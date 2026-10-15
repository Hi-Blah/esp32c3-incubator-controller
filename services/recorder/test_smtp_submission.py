import contextlib
import importlib.util
import io
import json
import smtplib
import ssl
import tempfile
import unittest
from email import policy
from email.parser import BytesParser
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("smtp_submission", Path(__file__).with_name("Send-IncubatorTestMail.py"))
submission = importlib.util.module_from_spec(spec)
spec.loader.exec_module(submission)


class FakeSMTP:
    def __init__(self):
        self.calls = []
        self.fail = None
        self.reject = None
        self.wire = None
        self.closed = False

    def step(self, name, code):
        self.calls.append(name)
        if self.fail == name:
            raise smtplib.SMTPServerDisconnected("TEST_AUTH_CODE a@example.invalid private")
        return (550 if self.reject == name else code), b"private response"

    def ehlo(self):return self.step("ehlo", 250)
    def starttls(self, **kwargs):return self.step("starttls", 220)
    def login(self, *args):return self.step("login", 235)
    def has_extn(self, name):return True
    def mail(self, address, options):return self.step("mail_from", 250)
    def rcpt(self, address):return self.step("rcpt_to", 250)
    def data(self, wire):
        self.wire = wire
        return self.step("data", 250)
    def quit(self):return self.step("quit", 221)
    def close(self):self.closed = True


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.secret = self.root / "secret"
        self.secret.write_text("TEST_AUTH_CODE", encoding="utf8")
        self.c = {"enabled": True, "host": "smtp.example.invalid", "port": 465, "security": "ssl",
                  "username": "a@example.invalid", "from": "a@example.invalid", "to": "b@example.invalid",
                  "password_file": str(self.secret)}
        self.config = self.root / "smtp.json"
        self.config.write_text(json.dumps(self.c), encoding="utf8")
        self.smtp = FakeSMTP()

    def tearDown(self):self.tmp.cleanup()

    def deliver(self, callback=None):
        with patch.object(submission.smtplib, "SMTP_SSL", return_value=self.smtp) as tls, \
             patch.object(submission.smtplib, "SMTP", return_value=self.smtp) as plain:
            result = submission.submit_smtp_message(self.c, "[警报] 测试标题", "温度25℃，检查光源。", callback)
        return result, tls, plain

    def test_headers_base64_ascii_crlf_and_chinese_roundtrip(self):
        sender, recipient, wire = submission.build_smtp_message(self.c, "测试标题", "温度25℃")
        self.assertEqual((sender, recipient), ("a@example.invalid", "b@example.invalid"))
        self.assertTrue(wire.isascii())
        self.assertNotIn(b"\n", wire.replace(b"\r\n", b""))
        msg = BytesParser(policy=policy.default).parsebytes(wire)
        self.assertTrue(msg["Date"]);self.assertTrue(msg["Message-ID"])
        self.assertEqual(msg["Content-Transfer-Encoding"], "base64")
        self.assertEqual(str(msg["Subject"]), "测试标题")
        self.assertIn("温度25℃", msg.get_content())

    def test_single_ssl_transaction_and_tls_verification(self):
        events = []
        result, tls, plain = self.deliver(events.append)
        self.assertEqual(self.smtp.calls, ["ehlo", "login", "mail_from", "rcpt_to", "data", "quit"])
        self.assertEqual(self.smtp.calls.count("data"), 1)
        self.assertEqual(result["smtp_acceptance"], "accepted")
        self.assertTrue(self.smtp.closed)
        self.assertEqual(tls.call_args.kwargs["context"].verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(tls.call_args.kwargs["context"].check_hostname)
        plain.assert_not_called()
        text = json.dumps(events)
        for value in ("TEST_AUTH_CODE", "a@example.invalid", "b@example.invalid", "private response"):
            self.assertNotIn(value, text)

    def test_rejected_recipient_does_not_submit_body(self):
        self.smtp.reject = "rcpt_to"
        with self.assertRaises(smtplib.SMTPResponseException) as result:self.deliver()
        self.assertNotIn("data", self.smtp.calls)
        self.assertEqual(result.exception.incubator_smtp_stage, "rcpt_to")
        self.assertEqual(result.exception.incubator_smtp_acceptance, "not_submitted")

    def test_sender_failure_is_located(self):
        self.smtp.fail = "mail_from"
        with self.assertRaises(smtplib.SMTPServerDisconnected) as result:self.deliver()
        self.assertEqual(result.exception.incubator_smtp_stage, "mail_from")
        self.assertEqual(result.exception.incubator_smtp_acceptance, "not_submitted")

    def test_data_disconnect_is_unknown_not_asserted_unsent(self):
        self.smtp.fail = "data"
        with self.assertRaises(smtplib.SMTPServerDisconnected) as result:self.deliver()
        self.assertEqual(result.exception.incubator_smtp_stage, "data")
        self.assertEqual(result.exception.incubator_smtp_acceptance, "unknown")
        self.assertEqual(self.smtp.calls.count("data"), 1)
        self.assertTrue(self.smtp.closed)

    def test_explicit_data_rejection_is_not_acceptance(self):
        self.smtp.reject = "data"
        with self.assertRaises(smtplib.SMTPDataError) as result:self.deliver()
        self.assertEqual(result.exception.incubator_smtp_acceptance, "rejected")

    def test_quit_disconnect_cannot_change_confirmed_acceptance(self):
        self.smtp.fail = "quit"
        result, _, _ = self.deliver()
        self.assertEqual(result["smtp_acceptance"], "accepted")
        self.assertTrue(self.smtp.closed)

    def test_starttls_order(self):
        self.c.update(security="starttls", port=587)
        result, tls, plain = self.deliver()
        self.assertEqual(self.smtp.calls, ["ehlo", "starttls", "ehlo", "login", "mail_from", "rcpt_to", "data", "quit"])
        self.assertEqual(result["smtp_acceptance"], "accepted")
        tls.assert_not_called();plain.assert_called_once()

    def test_no_explicit_flag_no_config_read_or_connection(self):
        output = io.StringIO()
        with patch.object(submission.Path, "read_text", side_effect=AssertionError("no read")), \
             patch.object(submission.smtplib, "SMTP_SSL", side_effect=AssertionError("no network")), \
             contextlib.redirect_stdout(output):
            self.assertEqual(submission.main([]), 0)
        self.assertEqual(json.loads(output.getvalue())["result"], "NO_ACTION")

    def test_cli_one_send_without_queue_modification(self):
        output = io.StringIO()
        before = {p.name:p.read_bytes() for p in self.root.iterdir()}
        with patch.object(submission.smtplib, "SMTP_SSL", return_value=self.smtp), contextlib.redirect_stdout(output):
            self.assertEqual(submission.main(["--send-test", "--config", str(self.config)]), 0)
        self.assertEqual(self.smtp.calls.count("data"), 1)
        self.assertEqual(json.loads(output.getvalue().splitlines()[-1])["result"], "SMTP_ACCEPTED")
        self.assertEqual(before, {p.name:p.read_bytes() for p in self.root.iterdir()})
        self.assertNotIn("TEST_AUTH_CODE", output.getvalue())

    def test_disabled_config_refuses_to_send(self):
        self.c["enabled"] = False
        with patch.object(submission.smtplib, "SMTP_SSL", side_effect=AssertionError("no network")):
            with self.assertRaises(ValueError):submission.submit_smtp_message(self.c, "test", "test")

    def test_multiple_recipients_are_never_silently_dropped(self):
        self.c["to"] = "b@example.invalid,c@example.invalid"
        with self.assertRaises(ValueError):submission.build_smtp_message(self.c, "test", "test")


if __name__ == "__main__":unittest.main(verbosity=2)
