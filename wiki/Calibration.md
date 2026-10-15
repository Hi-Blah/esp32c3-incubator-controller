# Optical calibration and notifications

Power the LDR modules from 3.3 V, verify changing raw ADC values and isolate each detector from the other light as far as the experiment requires. Relative ADC response is nonlinear and is not calibrated lux.

Quick capture: switch both lights OFF for dark, then the selected light ON with PWM disabled and the other light OFF for full-bright. Enable NAS monitoring only after meaningful baselines exist. Recalibrate after moving a sensor, changing a light board or changing its geometry.

Automatic calibration pauses the experiment, captures dark/full-bright and the frequency/duty grid, saves raw sums/counts to Flash, and lets NAS fit/interpolate. The grid is 12 frequencies × 7 duties plus dark/full-bright = 86 points per channel. It requires fresh NAS handshake, adequate RAM and a drained event backlog. Low RAM/backlog pauses outputs and preserves completed points; resuming resamples the interrupted point. It does not swap task stacks into Flash. Completion/cancel ends locked. Full grid acceptance with all intended hardware remains a release check.

High frequency at low duty produces very short pulses. LEDC is the PWM engine; RMT is used for bounded DHT11 reception. A MOS trigger module/LED driver can fail to respond optically to a 20 kHz, 1% command even when the timer accepts the configuration. Duty is not a linear power/brightness measurement. NAS uses measured model points; low-duty unreliable regions are not claimed to be accurate.

Fault policy on NAS: manual ON with sustained no-light can alert; manual OFF with detected light is exempt. Daily/cycle operation also detects stuck light after OFF and reduced response while ON, subject to calibration, thresholds, hold time and low-duty suppression. Delayed historical samples never trigger these live decisions. Connection loss/recovery creates notices; it is not automatically classified as a power outage.

SMTP is a separate mail worker. fnOS system notification settings are not inherited. For QQ use smtp.qq.com, port 465, security ssl, sender equal to the authenticated account, and the service authorization code in the local secret file. Never put it in a screenshot/issue or command arguments. Configure smtp.json enabled=true locally and start the mail profile. Inspect notifier.py --check and NAS mail status first. For an explicitly requested one-message delivery test run the bundled Send-IncubatorTestMail.py with --help, then --send-test following its options. A login-only probe does not verify actual message delivery.

SMTP acceptance is recorded separately from recipient receipt. Pending/failed notifications need mail-worker logs; after SMTP DATA acceptance, a failure to QUIT must not cause automatic duplicate sending. Verify one real optical-fault email and recovery notice before unattended use.

