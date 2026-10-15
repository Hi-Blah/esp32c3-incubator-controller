import contextlib,io,json,smtplib,sqlite3,sys,unittest
from pathlib import Path
from unittest.mock import patch
import test_optical_store as helpers
import notifier, optical_store
class Tests(unittest.TestCase):
 setUp=helpers.Tests.setUp
 tearDown=helpers.Tests.tearDown
 def deliver(self,*args):self.sent+=1
 def make(self):
  self.sent=0;optical_store.enqueue(self.s.db,"a","photo",1,100,"fault","test");self.s.db.commit()
 def test_disabled_no_send(self):
  self.make();self.assertFalse(notifier.dispatch(self.s.db,{"enabled":False},200,self.deliver));self.assertEqual(self.sent,0)
 def test_dispatch(self):
  self.make();self.assertTrue(notifier.dispatch(self.s.db,{"enabled":True},200,self.deliver));self.assertEqual(self.sent,1);self.assertEqual(self.s.db.execute("SELECT status FROM notifications").fetchone()[0],"sent")
 def test_failure_retry(self):
  self.make()
  def fail(*args):raise OSError("sensitive error")
  notifier.dispatch(self.s.db,{"enabled":True},200,fail);r=self.s.db.execute("SELECT status,attempts,last_error FROM notifications").fetchone();self.assertEqual(r,("pending",1,"OSError"));self.assertFalse(notifier.dispatch(self.s.db,{"enabled":True},201,self.deliver))
 def test_cooldown(self):
  self.make();notifier.dispatch(self.s.db,{"enabled":True},200,self.deliver);optical_store.enqueue(self.s.db,"b","photo",1,300,"fault","test");notifier.dispatch(self.s.db,{"enabled":True},300,self.deliver);self.assertEqual(self.sent,1)
 def config(self,enabled=True):
  root=Path(self.tmp.name);secret=root/'secret';secret.write_text('TEST_ONLY_AUTH_CODE',encoding='utf8')
  c={'enabled':enabled,'host':'smtp.example.invalid','port':465,'security':'ssl','from':'a@example.invalid','to':'b@example.invalid','username':'a@example.invalid','password_file':str(secret)}
  target=root/'smtp.json';target.write_text(json.dumps(c),encoding='utf8');return target,c
 def test_smtp_error_codes_do_not_expose_replies(self):
  self.make()
  def fail(*args):raise smtplib.SMTPAuthenticationError(535,b'secret auth-code and mailbox@example.invalid')
  notifier.dispatch(self.s.db,{'enabled':True},200,fail)
  r=self.s.db.execute('SELECT status,attempts,last_error FROM notifications').fetchone()
  self.assertEqual(r,('pending',1,'SMTPAuthenticationError SMTP=535'))
 def test_twelve_failed_attempts_stop_retrying(self):
  self.make()
  def fail(*args):raise TimeoutError()
  now=200
  for _ in range(12):
   notifier.dispatch(self.s.db,{'enabled':True},now,fail);now=self.s.db.execute('SELECT due_ms FROM notifications').fetchone()[0]
  self.assertEqual(self.s.db.execute('SELECT status,attempts FROM notifications').fetchone(),('failed',12))
  self.assertFalse(notifier.dispatch(self.s.db,{'enabled':True},now,self.deliver))
 def test_status_reads_old_database_without_migration(self):
  self.make();d=notifier.mail_status(self.s.db,200)
  self.assertEqual(d['status'],'unreported');self.assertEqual(d['pending_due'],1)
  self.assertFalse(self.s.db.execute("SELECT name FROM sqlite_master WHERE name='notification_runtime'").fetchone())
  self.assertNotIn('body',d['recent'][0])
 def test_worker_heartbeat_disabled_configuration_error_and_stale(self):
  self.make();notifier.heartbeat(self.s.db,'disabled',200)
  self.assertEqual(notifier.mail_status(self.s.db,300)['status'],'disabled')
  notifier.heartbeat(self.s.db,'configuration_error',500,'FileNotFoundError')
  self.assertEqual(notifier.mail_status(self.s.db,1000)['error'],'FileNotFoundError')
  self.assertEqual(notifier.mail_status(self.s.db,46000)['status'],'stale')
 def test_read_only_check_never_sends_or_changes_database(self):
  self.make();target,_=self.config();out=io.StringIO();dbfile=self.s.db.execute('PRAGMA database_list').fetchone()[2]
  with patch.object(sys,'argv',['notifier','--config',str(target),'--db',dbfile,'--check']),patch.object(notifier,'send',side_effect=AssertionError('must not send')),patch.object(notifier.smtplib,'SMTP_SSL',side_effect=AssertionError('must not connect')),contextlib.redirect_stdout(out):notifier.main()
  d=json.loads(out.getvalue());self.assertEqual(d['configuration']['status'],'ready');self.assertFalse(d['mail_sent']);self.assertFalse(d['smtp_connection_tested'])
  self.assertEqual(self.s.db.execute('SELECT status,attempts FROM notifications').fetchone(),('pending',0))
  self.assertFalse(self.s.db.execute("SELECT name FROM sqlite_master WHERE name='notification_runtime'").fetchone())
  self.assertNotIn('TEST_ONLY_AUTH_CODE',out.getvalue());self.assertNotIn('a@example.invalid',out.getvalue())
 def test_disabled_and_missing_secret_checks_are_safe(self):
  target,c=self.config(False);self.assertEqual(notifier.check_configuration(target)[1]['status'],'disabled')
  c['enabled']=True;Path(c['password_file']).unlink();target.write_text(json.dumps(c),encoding='utf8')
  self.assertEqual(notifier.check_configuration(target)[1],{'status':'configuration_error','error':'FileNotFoundError errno=2'})
 def test_manual_optical_alert_dispatches_without_real_mail(self):
  import nas_analysis as a
  from test_nas_analysis import state
  from test_record_store import NOW
  with self.s.db:
   a.settings(self.s.db,'a1b2c3',1);self.s.db.execute('UPDATE optical_settings SET dark=100,bright=2100,enabled=1 WHERE channel=1')
  for t in (1000,5000):self.s.state(state(t,mode=0,output=True,raw=100),NOW+t)
  sent=[];notifier.dispatch(self.s.db,{'enabled':True},NOW+5001,lambda *args:sent.append(args[1:]))
  self.assertEqual(len(sent),1);self.assertIn('MISSING_LIGHT',sent[0][1]);self.assertEqual(self.s.db.execute('SELECT status FROM notifications').fetchone()[0],'sent')
 def test_actual_sender_builds_complete_ascii_mime(self):
  from test_smtp_submission import FakeSMTP
  from email.parser import BytesParser
  from email import policy
  _,c=self.config();smtp=FakeSMTP()
  with patch.object(notifier.smtplib,'SMTP_SSL',return_value=smtp):notifier.send(c,'[警报] 测试','检测到开灯无光')
  self.assertTrue(smtp.wire.isascii());m=BytesParser(policy=policy.default).parsebytes(smtp.wire)
  self.assertTrue(m['Date']);self.assertTrue(m['Message-ID']);self.assertEqual(m['Content-Transfer-Encoding'],'base64')
  self.assertIn('检测到开灯无光',m.get_content())
 def test_data_disconnect_keeps_pending_and_reports_unknown(self):
  from test_smtp_submission import FakeSMTP
  self.make();_,c=self.config();smtp=FakeSMTP();smtp.fail='data'
  with patch.object(notifier.smtplib,'SMTP_SSL',return_value=smtp):notifier.dispatch(self.s.db,c,200)
  row=self.s.db.execute('SELECT status,attempts,last_error FROM notifications').fetchone()
  self.assertEqual(row,('pending',1,'SMTPServerDisconnected stage=data acceptance=unknown'));self.assertEqual(smtp.calls.count('data'),1)
 def test_quit_failure_after_data_does_not_queue_duplicate(self):
  from test_smtp_submission import FakeSMTP
  self.make();_,c=self.config();smtp=FakeSMTP();smtp.fail='quit'
  with patch.object(notifier.smtplib,'SMTP_SSL',return_value=smtp):notifier.dispatch(self.s.db,c,200)
  self.assertEqual(self.s.db.execute('SELECT status,attempts,last_error FROM notifications').fetchone(),('sent',1,None))
 def test_diagnostic_and_production_smtp_core_are_identical(self):
  from pathlib import Path
  source=Path(__file__).with_name('Send-IncubatorTestMail.py').read_text(encoding='utf8')
  core=source.split('# BEGIN SHARED SMTP CORE\n')[1].split('# END SHARED SMTP CORE')[0]
  self.assertIn(core,Path(notifier.__file__).read_text(encoding='utf8'))
if __name__=="__main__":unittest.main()
