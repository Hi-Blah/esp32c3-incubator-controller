#include "network_time.h"
#include "sample_journal.h"
#include "device_identity.h"
#include "control_service.h"
#include "module_config.h"
#include "health_service.h"
#include "log_health.h"
#include "light_feedback.h"
#if INC_ENABLE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#endif
#if INC_ENABLE_DHT
#include "bounded_dht.h"
#endif
#include "event_log.h"
#include "mqtt_service.h"
#include "network_budget.h"
#include "mqtt_raw_tcp.h"
#include "format_decimal.h"
#include "pwm_result.h"
#include "recovery_protocol.h"
#include "manual_recovery.h"
#include "channel_recovery.h"
#include "daily_recovery.h"
#include "auto_calibration.h"
#include <esp_random.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>

#if INC_ENABLE_DHT && (!defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT)
#error "Enable USB CDC On Boot: GPIO21 is DHT DAT, not UART0 TX"
#endif

namespace {
constexpr uint8_t LIGHT_PIN[2] = {INC_LIGHT1_PIN,INC_LIGHT2_PIN};
#if INC_ENABLE_PHOTO
constexpr uint8_t BUTTON_PIN[1]={INC_PAGE_BUTTON_PIN};
#else
constexpr uint8_t BUTTON_PIN[3]={INC_LIGHT1_BUTTON_PIN,INC_LIGHT2_BUTTON_PIN,INC_PAGE_BUTTON_PIN};
#endif
constexpr uint8_t MAX_ACTIVE_CHANNELS = 2; // User-requested dual-channel operation; PSU rating is shared by both outputs.
constexpr uint32_t SETTINGS_SCHEMA = 0xC3062001UL, JOURNAL_SCHEMA = 0xC3062002UL;
Preferences settingsPrefs;
struct Settings {
  uint32_t schema, revision;
  RecoveryPolicy recovery;
  program::Config config[2];
} settings = {};
// Commands execute serially on inc-controller; rollback storage must not occupy its stack.
Settings settingsBeforeEdit={};
program::Config editCandidate={}; // inc-controller only; avoid large command stack copies.
TaskHandle_t loopTask=nullptr;
struct Journal {
  uint32_t schema, revision;
  uint64_t epochMs, elapsed[2];
  bool armed, running[2];
} journal = {};
struct JournalTag {uint32_t schema;Journal saved;char boot[40];};
constexpr uint32_t JOURNAL_TAG_SCHEMA=0xC3062003;
struct ManualSaved {uint32_t schema,revision;bool armed;uint8_t mask;};
constexpr uint32_t MANUAL_SCHEMA=0xC3062802;
ManualSaved manualSaved={};bool manualRecoveryEnabled=false,manualPending=false;
bool nasRecoveryEnabled=false,nasWaiting=false,nasTried=false;
uint64_t nasDeadline=0,estimatedOutageMs=0,startupWaitMs=0;
char previousBoot[40]={},currentBoot[40]={},recoveryReq[17]={},recoverySource[16]="NONE";
program::Runtime runtime[2] = {};
program::Progress progress[2] = {};
bool output[2] = {}, enabled[2] = {true,true};uint64_t outputTransitions[2]={},outputChangedAt[2]={};
bool armed = false, logicPassed = false, storageReady = false;
bool resumeWaiting = false, recovered = false, checkpointDirty = false;
channelpolicy::Saved policySettings={};uint8_t recoveryMask=0;
dailyrestart::Preferences dailyPreferences={};uint8_t dailyPendingMask=0;
uint32_t observedLogFailures=0;
uint64_t perGap[2]={},perOutage[2]={},perStartup[2]={};
char perSource[2][16]={"NONE","NONE"};
uint64_t checkpointEpochMs = 0, recoveryGapMs = 0, lastCheckpointMono = 0;
ClockSource clockSource = ClockSource::Unsynced;
uint64_t nasClockOffset=0;
QueueHandle_t commands = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
ControlSnapshot shared = {};
struct Sensor { float t, h; bool valid, oled; uint64_t sampledAtMs, sampleEpochMs; bool timeTrusted; } sensor = {};
bool pwmReady[2] = {};
uint8_t currentDuty[2] = {};
bool pwmSettingsDirty = false;
uint64_t pwmChangedAt = 0;
void publish();
void applyOutputs();
ControlSnapshot& controllerEventSnapshot(){static ControlSnapshot s={};controlRead(s);return s;}
struct OpticalCalibration {bool active=false,paused=false;uint8_t mask=0;uint16_t index=0,total=0;uint32_t settleMs=2000,sampleMs=2000;uint64_t stageAt=0,lastSample=0;uint32_t sum[2]={};uint16_t count=0;char id[32]={};} optical;

program::Config opticalOutput[2]={};
void opticalCancel(const char *reason);
void opticalTick();
bool finishedReported[2] = {};
#if INC_ENABLE_OLED
Adafruit_SSD1306 oled(128,64,&Wire,-1);
U8G2_FOR_ADAFRUIT_GFX unicode;
#endif
#if INC_ENABLE_DHT
BoundedDht11 dht;
#endif
bool oledReady = false;
uint8_t page = 0;
uint32_t lastSensor = 0, lastDisplay = 0, lastPage = 0;
uint64_t monoMs() { return static_cast<uint64_t>(esp_timer_get_time())/1000ULL; }
uint64_t epochMs() { timeval tv = {}; gettimeofday(&tv,nullptr); return static_cast<uint64_t>(tv.tv_sec)*1000ULL+tv.tv_usec/1000; }
void usb(const char *format, ...) {
  if (!Serial) return;
  char b[1024]; va_list args; va_start(args,format); vsnprintf(b,sizeof(b),format,args); va_end(args);
  Serial.write(reinterpret_cast<uint8_t *>(b),strlen(b));
}
struct MemoryStage{const char *name;uint32_t before,after,largest;};
MemoryStage memoryStages[9]={};uint8_t memoryStageCount=0;
void memoryStage(const char *name,uint32_t before){if(memoryStageCount<9)memoryStages[memoryStageCount++]={name,before,ESP.getFreeHeap(),ESP.getMaxAllocHeap()};}
void memoryInfo(){const uint32_t total=ESP.getHeapSize(),free=ESP.getFreeHeap();usb("MEM_HEAP total=%lu allocated=%lu free=%lu minimum=%lu largest=%lu\n",static_cast<unsigned long>(total),static_cast<unsigned long>(total-free),static_cast<unsigned long>(free),static_cast<unsigned long>(ESP.getMinFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()));for(uint8_t i=0;i<memoryStageCount;i++){const auto &s=memoryStages[i];usb("MEM_STAGE module=%s before=%lu after=%lu net_delta=%ld largest_after=%lu\n",s.name,static_cast<unsigned long>(s.before),static_cast<unsigned long>(s.after),static_cast<long>(int64_t(s.before)-s.after),static_cast<unsigned long>(s.largest));}usb("MEM_NOTE startup deltas include concurrent drivers; static globals excluded from heap total. See build report for static RAM.\n");}
const char *modeName(program::Mode m) { return m == program::Mode::Daily ? "DAILY" : m == program::Mode::Cycle ? "CYCLE" : "MANUAL"; }
bool clockOk(tm &t) {
  time_t epoch = time(nullptr);
  return clockSource != ClockSource::Unsynced && localtime_r(&epoch,&t) && t.tm_year >= 124 && t.tm_year <= 199;
}
bool validPolicy(RecoveryPolicy p) {
  return p.checkpointSeconds >= 10 && p.checkpointSeconds <= 3600 && p.restartAfterSeconds <= 604800;
}
bool saveSettings() {
  bool ok=storageReady && settingsPrefs.putBytes("settings",&settings,sizeof(settings)) == sizeof(settings);
  if(ok)pwmSettingsDirty=false;
  return ok;
}
void syncPolicyFlags(){manualRecoveryEnabled=channelpolicy::anyManual(policySettings.channel);nasRecoveryEnabled=policySettings.channel[0].nasEnabled||policySettings.channel[1].nasEnabled;}
bool savePolicies(){const bool good=storageReady&&settingsPrefs.putBytes("policies-v21",&policySettings,sizeof(policySettings))==sizeof(policySettings);if(good)syncPolicyFlags();return good;}
void loadPolicies(){policySettings.schema=channelpolicy::SCHEMA;for(auto &p:policySettings.channel)p={settings.recovery,manualRecoveryEnabled,nasRecoveryEnabled};channelpolicy::Saved saved={};if(storageReady&&settingsPrefs.getBytesLength("policies-v21")==sizeof(saved)&&settingsPrefs.getBytes("policies-v21",&saved,sizeof(saved))==sizeof(saved)&&saved.schema==channelpolicy::SCHEMA&&channelpolicy::valid(saved.channel[0])&&channelpolicy::valid(saved.channel[1]))policySettings=saved;else if(storageReady&&!savePolicies())storageReady=false;syncPolicyFlags();}
bool saveDailyPreferences(){return storageReady&&settingsPrefs.putBytes("daily-v211",&dailyPreferences,sizeof(dailyPreferences))==sizeof(dailyPreferences);}
void loadDailyPreferences(){dailyPreferences={};if(!storageReady)return;if(!settingsPrefs.isKey("daily-v211")){if(!saveDailyPreferences())storageReady=false;return;}dailyrestart::Preferences saved={};if(settingsPrefs.getBytesLength("daily-v211")==sizeof(saved)&&settingsPrefs.getBytes("daily-v211",&saved,sizeof(saved))==sizeof(saved)&&dailyrestart::valid(saved))dailyPreferences=saved;else storageReady=false;}
bool cycleRecoveryEnabled(){return channelpolicy::anyCycle(policySettings.channel);}
void loadSettings() {
  settings.schema = SETTINGS_SCHEMA; settings.revision = 1;
  settings.recovery = {false,true,0,60};
  settings.config[0] = settings.config[1] = program::defaults();
  storageReady = settingsPrefs.begin("inc-con",false);
  Settings saved = {};
  if (storageReady && settingsPrefs.getBytesLength("settings") == sizeof(saved) &&
      settingsPrefs.getBytes("settings",&saved,sizeof(saved)) == sizeof(saved) &&
      saved.schema == SETTINGS_SCHEMA && validPolicy(saved.recovery) &&
      program::valid(saved.config[0]) && program::valid(saved.config[1])) settings = saved;
  else {
    // Migrate configuration only. Resume gates are applied below after validating reset and preferences.
    bool migrated=false;
    Preferences previous;
    Settings prior={};
    if(previous.begin("inc-adv",true)){
      if(previous.getBytesLength("settings")==sizeof(prior)&&previous.getBytes("settings",&prior,sizeof(prior))==sizeof(prior)&&prior.schema==SETTINGS_SCHEMA&&validPolicy(prior.recovery)&&program::valid(prior.config[0])&&program::valid(prior.config[1])){settings=prior;migrated=true;}
      previous.end();
    }
    // Read the earlier format without modifying its keys, preserving rollback.
    struct Legacy { uint32_t schema; uint8_t mode[2]; uint16_t onMinute[2],offMinute[2]; uint32_t onSeconds[2],offSeconds[2]; } old = {};
    Preferences legacy;
    if (!migrated && legacy.begin("incubator",true)) {
      if (legacy.getBytesLength("settings") == sizeof(old) && legacy.getBytes("settings",&old,sizeof(old)) == sizeof(old) && old.schema == 0xC3060001UL) {
        for (uint8_t i=0;i<2;++i) {
          auto c = program::defaults(); c.mode = static_cast<program::Mode>(old.mode[i]);
          c.daily[0] = {static_cast<uint32_t>(old.onMinute[i])*60,static_cast<uint32_t>(old.offMinute[i])*60};
          c.steps[0] = {old.onSeconds[i]*1000UL,old.offSeconds[i]*1000UL};
          if (program::valid(c)) settings.config[i] = c;
        }
      }
      legacy.end();
    }
    saveSettings();
  }
  if (storageReady && settingsPrefs.getBytesLength("journal") == sizeof(journal) &&
      settingsPrefs.getBytes("journal",&journal,sizeof(journal)) == sizeof(journal) &&
      journal.schema == JOURNAL_SCHEMA && journal.revision == settings.revision &&
      journal.epochMs >= 1704067200000ULL) {
    for (uint8_t i=0;i<2;++i) if (settings.recovery.enabled && journal.armed && journal.running[i] && settings.config[i].mode == program::Mode::Cycle) resumeWaiting = true;
    checkpointEpochMs = journal.epochMs;
  }
  // Explicit 2.0.9 upgrade preference. Commit marker last; preserve subsequent user choices.
  // Start this migration locked instead of reviving a stale manual snapshot.
  if(storageReady&&!settingsPrefs.getBool("manual-v209",false)){
    ManualSaved initial={};initial.schema=MANUAL_SCHEMA;initial.revision=settings.revision;
    const bool ok=settingsPrefs.putBytes("manual-last",&initial,sizeof(initial))==sizeof(initial)&&
      settingsPrefs.putBool("manual-resume",true)==1&&settingsPrefs.putBool("manual-v209",true)==1;
    manualSaved=initial;manualPending=false;
    if(!ok)storageReady=false;
  }
  manualRecoveryEnabled=storageReady&&settingsPrefs.getBool("manual-resume",true);
  if(storageReady&&settingsPrefs.getBytesLength("manual-last")==sizeof(manualSaved)&&settingsPrefs.getBytes("manual-last",&manualSaved,sizeof(manualSaved))==sizeof(manualSaved)&&manualSaved.schema==MANUAL_SCHEMA&&manualSaved.revision==settings.revision&&manualSaved.mask<=3)manualPending=manualrecovery::mask(manualRecoveryEnabled,manualSaved.armed,manualSaved.revision,settings.revision,manualSaved.mask,(settings.config[0].mode==program::Mode::Manual?1:0)|(settings.config[1].mode==program::Mode::Manual?2:0),false)!=0;
  nasRecoveryEnabled=storageReady&&settingsPrefs.getBool("nas-resume",false);
  loadPolicies();loadDailyPreferences();
  recoveryMask=channelpolicy::resumeMask(policySettings.channel,journal.armed,journal.running,settings.config);
  resumeWaiting=journal.schema==JOURNAL_SCHEMA&&journal.revision==settings.revision&&journal.epochMs>=1704067200000ULL&&recoveryMask;
  const uint8_t allowedManual=(policySettings.channel[0].manualEnabled?1:0)|(policySettings.channel[1].manualEnabled?2:0);
  manualPending=storageReady&&manualSaved.schema==MANUAL_SCHEMA&&manualSaved.revision==settings.revision&&manualSaved.armed&&bool(manualSaved.mask&allowedManual&((settings.config[0].mode==program::Mode::Manual?1:0)|(settings.config[1].mode==program::Mode::Manual?2:0)));
  enabled[0]=enabled[1]=false;
  JournalTag tag={};
  if(storageReady&&settingsPrefs.getBytesLength("journal-nas")==sizeof(tag)&&settingsPrefs.getBytes("journal-nas",&tag,sizeof(tag))==sizeof(tag)&&tag.schema==JOURNAL_TAG_SCHEMA&&
     tag.saved.schema==journal.schema&&tag.saved.revision==journal.revision&&tag.saved.epochMs==journal.epochMs&&tag.saved.armed==journal.armed&&
     tag.saved.elapsed[0]==journal.elapsed[0]&&tag.saved.elapsed[1]==journal.elapsed[1]&&tag.saved.running[0]==journal.running[0]&&tag.saved.running[1]==journal.running[1]&&
     memchr(tag.boot,0,sizeof(tag.boot))&&recovery::token(tag.boot,10,39))snprintf(previousBoot,sizeof(previousBoot),"%s",tag.boot);
  // First boot of the stack fix discards resume authorization, preserving all configured programs.
  if(storageReady&&settingsPrefs.getUInt("stackfix",0)!=1){
    resumeWaiting=false;recoveryMask=0;journal={};checkpointEpochMs=0;
    bool resetOk=settingsPrefs.putBytes("journal",&journal,sizeof(journal))==sizeof(journal);
    if(!resetOk||settingsPrefs.putUInt("stackfix",1)!=sizeof(uint32_t))storageReady=false;
  }
  auto reset=esp_reset_reason();
  if(reset==ESP_RST_PANIC||reset==ESP_RST_INT_WDT||reset==ESP_RST_TASK_WDT||reset==ESP_RST_WDT||reset==ESP_RST_BROWNOUT){resumeWaiting=false;recoveryMask=0;manualPending=false;checkpointDirty=true;}
  dailyPendingMask=dailyrestart::bootMask(dailyPreferences,settings.config,reset!=ESP_RST_PANIC&&reset!=ESP_RST_INT_WDT&&reset!=ESP_RST_TASK_WDT&&reset!=ESP_RST_WDT&&reset!=ESP_RST_BROWNOUT);
}
bool saveManual(){
 ManualSaved next={};next.schema=MANUAL_SCHEMA;next.revision=settings.revision;next.armed=armed;
 for(uint8_t i=0;i<2;++i)if(armed&&enabled[i]&&settings.config[i].mode==program::Mode::Manual&&runtime[i].manualOn&&policySettings.channel[i].manualEnabled)next.mask|=1U<<i;
 if(next.schema==manualSaved.schema&&next.revision==manualSaved.revision&&next.armed==manualSaved.armed&&next.mask==manualSaved.mask)return true;
 if(!storageReady||settingsPrefs.putBytes("manual-last",&next,sizeof(next))!=sizeof(next)){healthLatch(FAULT_NVS);return false;}manualSaved=next;return true;
}
void restoreManual(){if(!manualPending)return;manualPending=false;for(uint8_t i=0;i<2;++i)if(settings.config[i].mode==program::Mode::Manual){const bool on=policySettings.channel[i].manualEnabled&&manualSaved.armed&&(manualSaved.mask&(1U<<i));runtime[i].manualOn=on;runtime[i].running=false;enabled[i]=on;if(on)snprintf(perSource[i],sizeof(perSource[i]),"LOCAL_MANUAL");}armed=(enabled[0]||enabled[1])&&logicPassed;if(!resumeWaiting)snprintf(recoverySource,sizeof(recoverySource),"LOCAL_MANUAL");checkpointDirty=true;}
__attribute__((noinline)) void restoreDaily(){
 if(!dailyPendingMask)return;
 tm t={};if(!dailyrestart::canApply(clockOk(t),logicPassed,storageReady,optical.active))return;
 const uint8_t mask=dailyPendingMask;dailyPendingMask=0;
 for(unsigned i=0;i<2;++i)if(mask&(1U<<i)){if(settings.config[i].mode!=program::Mode::Daily||!(dailyPreferences.mask&(1U<<i)))continue;enabled[i]=true;runtime[i]={false,true,monoMs(),0};finishedReported[i]=false;snprintf(perSource[i],sizeof(perSource[i]),"%s",clockSource==ClockSource::Nas?"NAS_DAILY":clockSource==ClockSource::Ntp?"NTP_DAILY":"MANUAL_DAILY");usb("DAILY_RESTORE L%u clock=%s evaluate_current_time\n",i+1,clockSourceName(clockSource));}
 armed=(enabled[0]||enabled[1])&&logicPassed;checkpointDirty=true;
 if(!resumeWaiting&&!strcmp(recoverySource,"NONE"))snprintf(recoverySource,sizeof(recoverySource),"DAILY_TIME");
 applyOutputs();publish();eventEmit("daily_restore","CURRENT_TIME",controllerEventSnapshot(),true);
}
bool checkpoint() {
  tm t = {};
  if (!storageReady || !saveManual()) return false;
  // Only inc-controller invokes checkpoints; fixed buffers keep NVS paths within stack budget.
  static Journal next={};static JournalTag tag={};
  next={}; next.schema = JOURNAL_SCHEMA; next.revision = settings.revision;
  next.epochMs = clockOk(t) ? epochMs() : 0; next.armed = armed;
  for (uint8_t i=0;i<2;++i) {
    const auto p = program::evaluate(settings.config[i],runtime[i],monoMs(),true,0);
    next.elapsed[i] = p.elapsedMs;
    next.running[i] = enabled[i] && runtime[i].running && !p.finished && settings.config[i].mode == program::Mode::Cycle && policySettings.channel[i].cycle.enabled;
  }
  if (settingsPrefs.putBytes("journal",&next,sizeof(next)) != sizeof(next)) return false;
  tag={};tag.schema=JOURNAL_TAG_SCHEMA;tag.saved=next;snprintf(tag.boot,sizeof(tag.boot),"%s",currentBoot);
  if(recovery::token(tag.boot,10,39)&&settingsPrefs.putBytes("journal-nas",&tag,sizeof(tag))!=sizeof(tag))usb("WARN NAS checkpoint identity write failed; local progress saved\n");
  journal = next; checkpointEpochMs = next.epochMs; lastCheckpointMono = monoMs(); checkpointDirty = false;
  publish();
  return true;
}
bool configurePwm(uint8_t ch) {
  const auto &c = optical.active?opticalOutput[ch]:settings.config[ch];
  if (!c.pwm) return true;
  // Use a dedicated IDF timer per channel: frequencies can change independently.
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.timer_num = static_cast<ledc_timer_t>(ch);
  timer.duty_resolution = LEDC_TIMER_10_BIT; timer.freq_hz = c.frequency; timer.clk_cfg = LEDC_USE_XTAL_CLK;
  esp_err_t timerResult=ledc_timer_config(&timer);if(timerResult!=ESP_OK){usb("PWM_DIAG light=%u stage=timer error=%s (%d)\n",ch+1,esp_err_to_name(timerResult),timerResult);return false;}
  ledc_channel_config_t channel = {};
  channel.gpio_num = LIGHT_PIN[ch]; channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = static_cast<ledc_channel_t>(ch); channel.timer_sel = static_cast<ledc_timer_t>(ch);
  channel.duty = 0; channel.hpoint = 0; channel.intr_type = LEDC_INTR_DISABLE;
  esp_err_t channelResult=ledc_channel_config(&channel);pwmReady[ch]=channelResult==ESP_OK;if(!pwmReady[ch])usb("PWM_DIAG light=%u stage=channel error=%s (%d)\n",ch+1,esp_err_to_name(channelResult),channelResult);
  currentDuty[ch]=0;
  return pwmReady[ch];
}
bool drive(uint8_t ch, bool on) {
  const auto &c = optical.active?opticalOutput[ch]:settings.config[ch];
  if (c.pwm && pwmReady[ch]) {
    const uint8_t duty = on ? c.duty : 0;
    if (currentDuty[ch] != duty) {
      if (!on) {
        // Disable the waveform immediately; zero duty may wait for a PWM period.
        if (ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(ch),0) != ESP_OK) return false;
      } else if (ledc_set_duty(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(ch),(1024UL*duty+50)/100) != ESP_OK ||
                 ledc_update_duty(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(ch)) != ESP_OK) return false;
      currentDuty[ch] = duty;
    }
  } else {
    if (c.pwm && on) return false;
    if (pwmReady[ch]) { ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(ch),0);gpio_reset_pin(static_cast<gpio_num_t>(LIGHT_PIN[ch]));pinMode(LIGHT_PIN[ch],OUTPUT);digitalWrite(LIGHT_PIN[ch],LOW);currentDuty[ch]=0;pwmReady[ch]=false; }
    const uint8_t duty = on?100:0;
    if(currentDuty[ch]!=duty){pinMode(LIGHT_PIN[ch],OUTPUT);digitalWrite(LIGHT_PIN[ch],on?HIGH:LOW);currentDuty[ch]=duty;}
  }
  return true;
}
void applyOutputs() {
  tm t = {}; bool timeValid = clockOk(t);
  const uint32_t sec = t.tm_hour*3600+t.tm_min*60+t.tm_sec;
  bool want[2]; const uint64_t now = monoMs();
  bool changed[2]={};
  for (uint8_t i=0;i<2;++i) {
    progress[i] = program::evaluate(settings.config[i],runtime[i],now,timeValid,sec);
    if(!enabled[i])progress[i].requested=false;
    want[i] = progress[i].requested;
    if (progress[i].finished && !finishedReported[i]) { finishedReported[i] = true; checkpointDirty = true; }
  }
  if(optical.active){const auto point=autocal::point(optical.mask,optical.index);want[0]=want[1]=false;if(!optical.paused&&!point.dark&&!point.done)want[point.channel]=true;}
  bool next[2]; program::limited(want,output,(armed||optical.active)&&logicPassed,MAX_ACTIVE_CHANNELS,next);
  bool ok = true;
  for (uint8_t i=0;i<2;++i) if (!next[i] && !drive(i,false)) ok = false; // Break before make.
  if (ok) for (uint8_t i=0;i<2;++i) if (next[i] && !drive(i,true)) ok = false;
  if (!ok) {
    armed = false; checkpointDirty = true;if(optical.active)opticalCancel("PWM output failure");
    for (uint8_t i=0;i<2;++i) { ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(i),0);gpio_reset_pin(static_cast<gpio_num_t>(LIGHT_PIN[i]));pinMode(LIGHT_PIN[i],OUTPUT);digitalWrite(LIGHT_PIN[i],LOW);currentDuty[i]=0;pwmReady[i]=false;next[i]=false; }
    healthLatch(FAULT_PWM);usb("ERR PWM output failed; LOCKED\n");
  }
  for (uint8_t i=0;i<2;++i) {
    if (output[i] != next[i]) usb("EVENT uptime_ms=%llu light=%u command=%s pwm=%u\n",now,i+1,next[i]?"ON":"OFF",settings.config[i].pwm);
    changed[i]=output[i]!=next[i];if(changed[i]){++outputTransitions[i];outputChangedAt[i]=now;}output[i] = next[i];
  }
  if(changed[0]||changed[1]){publish();for(uint8_t i=0;i<2;++i)if(changed[i])if(settings.config[i].mode==program::Mode::Manual&&!optical.active)eventEmit("light_output",i==0?"light1":"light2",controllerEventSnapshot(),false);}
}
void publish() {
  ControlSnapshot s = {};
  for (uint8_t i=0;i<2;++i) { s.config[i] = settings.config[i]; s.progress[i] = progress[i]; s.output[i] = output[i];s.transitions[i]=outputTransitions[i];s.lastSwitchMs[i]=outputChangedAt[i]; }
  s.armed = armed; s.logicPassed = logicPassed; s.storageReady = storageReady;
  s.resumeWaiting = resumeWaiting; s.recovered = recovered; s.recovery = policySettings.channel[0].cycle;s.recovery.enabled=cycleRecoveryEnabled();
  for(uint8_t i=0;i<2;++i){s.channelRecovery[i]=policySettings.channel[i].cycle;s.channelManualRecovery[i]=policySettings.channel[i].manualEnabled;s.channelNasRecovery[i]=policySettings.channel[i].nasEnabled;s.channelEnabled[i]=armed&&enabled[i];s.channelDailyRecovery[i]=bool(dailyPreferences.mask&(1U<<i));s.channelDailyWaiting[i]=bool(dailyPendingMask&(1U<<i));s.channelGapMs[i]=perGap[i];s.channelOutageMs[i]=perOutage[i];s.channelStartupMs[i]=perStartup[i];snprintf(s.channelRecoverySource[i],sizeof(s.channelRecoverySource[i]),"%s",perSource[i]);}
  s.limit = MAX_ACTIVE_CHANNELS; s.clockSource = clockSource;
  s.uptimeMs = monoMs(); s.checkpointEpochMs = checkpointEpochMs; s.recoveryGapMs = recoveryGapMs;
  tm t = {}; if (clockOk(t)) strftime(s.clockText,sizeof(s.clockText),"%Y-%m-%d %H:%M:%S",&t);
  s.manualRecoveryEnabled=manualRecoveryEnabled;
  for(uint8_t i=0;i<2;++i)s.actualFrequency[i]=pwmReady[i]?ledc_get_freq(LEDC_LOW_SPEED_MODE,static_cast<ledc_timer_t>(i)):0;
  s.configRevision=settings.revision;s.epochMs=networkClock(clockSource)?epochMs():0;
  s.estimatedOutageMs=estimatedOutageMs;s.startupWaitMs=startupWaitMs;s.nasRecoveryEnabled=nasRecoveryEnabled;s.nasWaiting=nasWaiting&&resumeWaiting;
  for(uint8_t i=0;i<2;++i)s.cycleRunning[i]=armed&&enabled[i]&&runtime[i].running&&!progress[i].finished&&settings.config[i].mode==program::Mode::Cycle;
  snprintf(s.previousBoot,sizeof(s.previousBoot),"%s",previousBoot);snprintf(s.recoveryReq,sizeof(s.recoveryReq),"%s",recoveryReq);snprintf(s.recoverySource,sizeof(s.recoverySource),"%s",recoverySource);
  portENTER_CRITICAL(&stateMux);
  s.temperature = sensor.t; s.humidity = sensor.h; s.sensorValid = sensor.valid && s.uptimeMs>=sensor.sampledAtMs && s.uptimeMs-sensor.sampledAtMs<15000; s.oledReady = sensor.oled;s.sampledAtMs=sensor.sampledAtMs;s.sampleEpochMs=sensor.sampleEpochMs;s.sampleTimeTrusted=sensor.timeTrusted;
  shared = s;
  portEXIT_CRITICAL(&stateMux);
}
bool number(const char *s,uint32_t max,uint32_t &n) {
  if (!s||!*s) return false; uint64_t v=0;
  for (const char *p=s;*p;++p) { if (*p<'0'||*p>'9') return false; v=v*10+(*p-'0'); if(v>max)return false; }
  n=static_cast<uint32_t>(v);return true;
}
bool seconds(const char *s,uint32_t &ms) {
  if (!s||!*s) return false;
  char b[16]; if(strlen(s)>=sizeof(b))return false; snprintf(b,sizeof(b),"%s",s);
  char *dot=strchr(b,'.');uint32_t whole=0,frac=0;
  if(dot) { *dot=0; if(strlen(dot+1)!=1||!number(dot+1,9,frac))return false; }
  if(!number(b,604800,whole))return false;
  const uint64_t v=static_cast<uint64_t>(whole)*1000+frac*100;
  if(v<program::MIN_MS||v>program::MAX_MS)return false;ms=v;return true;
}
bool daySecond(const char *s,uint32_t &n) {
  if(!s||(strlen(s)!=5&&strlen(s)!=8)||s[2]!=':'||(strlen(s)==8&&s[5]!=':'))return false;
  char b[3]={s[0],s[1],0};uint32_t h,m,sec=0;
  if(!number(b,23,h))return false;b[0]=s[3];b[1]=s[4];if(!number(b,59,m))return false;
  if(strlen(s)==8){b[0]=s[6];b[1]=s[7];if(!number(b,59,sec))return false;}
  n=h*3600+m*60+sec;return true;
}
bool setClock(const char *date,const char *clock) {
  if(strlen(date)!=10||date[4]!='-'||date[7]!='-'||strlen(clock)!=8)return false;
  uint32_t sec;if(!daySecond(clock,sec))return false;
  char y[5]={};memcpy(y,date,4);uint32_t year;if(!number(y,2099,year)||year<2024)return false;
  char b[3]={date[5],date[6],0};uint32_t month,day;
  if(!number(b,12,month)||!month)return false;b[0]=date[8];b[1]=date[9];if(!number(b,31,day)||!day)return false;
  tm t={};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=sec/3600;t.tm_min=(sec/60)%60;t.tm_sec=sec%60;
  time_t epoch=mktime(&t);tm check={};if(!localtime_r(&epoch,&check)||check.tm_year!=static_cast<int>(year-1900)||check.tm_mon!=static_cast<int>(month-1)||check.tm_mday!=static_cast<int>(day))return false;
  timeval tv={};tv.tv_sec=epoch;if(settimeofday(&tv,nullptr))return false;clockSource=ClockSource::Manual;return true;
}
__attribute__((noinline)) void finishRecovery(const Journal&remoteSaved,bool remote){
 const uint64_t now=epochMs(),uptime=monoMs();nasWaiting=false;bool any=enabled[0]||enabled[1],usedNAS=false,usedLocal=false;
 for(uint8_t i=0;i<2;++i){if(!(recoveryMask&(1U<<i)))continue;const bool useNAS=remote&&policySettings.channel[i].nasEnabled;const auto &saved=useNAS?remoteSaved:journal;const auto &policy=policySettings.channel[i].cycle;const auto plan=recovery::plan(saved.epochMs,saved.elapsed,now,uptime,policy.countOutage,policy.restartAfterSeconds,useNAS);if(!plan.valid){enabled[i]=false;runtime[i]={};snprintf(perSource[i],sizeof(perSource[i]),"CLOCK_ERROR");continue;}perGap[i]=plan.gap;perOutage[i]=plan.outage;perStartup[i]=plan.startup;enabled[i]=policy.enabled&&saved.armed&&saved.running[i]&&settings.config[i].mode==program::Mode::Cycle;runtime[i]={false,enabled[i],uptime,plan.offset[i]};if(enabled[i]&&program::evaluate(settings.config[i],runtime[i],uptime,true,0).finished)enabled[i]=runtime[i].running=false;snprintf(perSource[i],sizeof(perSource[i]),"%s",useNAS?"NAS":"LOCAL");usedNAS|=useNAS;usedLocal|=!useNAS;any|=enabled[i];usb("RECOVERY L%u source=%s gap_ms=%llu outage_ms=%llu startup_ms=%llu rule=%s\n",i+1,perSource[i],plan.gap,plan.outage,plan.startup,plan.restart?"RESTART":policy.countOutage?"ADVANCE":"PAUSE");}
 armed=any&&logicPassed;resumeWaiting=false;recoveryMask=0;recovered=true;checkpointDirty=true;snprintf(recoverySource,sizeof(recoverySource),"%s",usedNAS&&usedLocal?"MIXED":usedNAS?"NAS":usedLocal?"LOCAL":"CLOCK_ERROR");unsigned reference=perGap[1]>perGap[0]?1:0;recoveryGapMs=perGap[reference];estimatedOutageMs=perOutage[reference];startupWaitMs=perStartup[reference];applyOutputs();publish();eventEmit("recovery",usedNAS?"NAS_RESTORED":"LOCAL_RESTORED",controllerEventSnapshot(),true);
}
void recover(){
 if(!resumeWaiting||!networkClock(clockSource))return;
 if(channelpolicy::anyNAS(policySettings.channel,recoveryMask)&&mqttView().enabled&&!nasTried&&previousBoot[0]){
  nasTried=true;nasWaiting=true;nasDeadline=monoMs()+15000;snprintf(recoveryReq,sizeof(recoveryReq),"%08lx%08lx",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));snprintf(recoverySource,sizeof(recoverySource),"WAIT_NAS");publish();usb("RECOVERY waiting for NAS backup (max15s); waiting cycle channels OFF\n");return;
 }
 if(nasWaiting&&monoMs()<nasDeadline)return;
 if(nasWaiting)usb("RECOVERY NAS timeout; using local checkpoint\n");finishRecovery(journal,false);
}
__attribute__((noinline)) bool acceptRecovery(const char*payload,char*reply,size_t size){
 recovery::Reply r={};
 if(!resumeWaiting||!nasWaiting||monoMs()>=nasDeadline||!networkClock(clockSource)){snprintf(reply,size,"ERR recovery response no longer pending");return false;}
 if(!recovery::parse(payload,r)){snprintf(reply,size,"ERR invalid recovery response");return false;}
 if(!recovery::matches(r,recoveryReq,currentBoot,previousBoot,settings.revision)){snprintf(reply,size,"ERR recovery identity/config mismatch");return false;}
 if(!recovery::usable(r,journal.epochMs,epochMs(),monoMs())){finishRecovery(journal,false);snprintf(reply,size,"OK local fallback; NAS absent/older/invalid time");return true;}
 Journal saved=journal;saved.epochMs=r.epochMs;saved.elapsed[0]=r.elapsed[0];saved.elapsed[1]=r.elapsed[1];saved.armed=r.armed;
 for(uint8_t i=0;i<2;++i)saved.running[i]=journal.running[i]&&bool(r.running&(1U<<i));
 finishRecovery(saved,true);snprintf(reply,size,"OK NAS recovery applied once");return true;
}
void status(char *b,size_t size) {
  const auto net=networkView();const auto ble=bleView();tm t={};char clock[24]="UNSYNC";
  Sensor current;portENTER_CRITICAL(&stateMux);current=sensor;portEXIT_CRITICAL(&stateMux);
  if(clockOk(t))strftime(clock,sizeof(clock),"%Y-%m-%d %H:%M:%S",&t);
  char temperature[20],humidity[20];decimal::sample(temperature,sizeof(temperature),current.t,current.valid);decimal::sample(humidity,sizeof(humidity),current.h,current.valid);
  snprintf(b,size,"STATUS firmware=Connected-2.2.3 clock=%s source=%s armed=%u limit=%u logic=%s nvs=%u\nENV %sC %s%% valid=%u\nL1 %s %s row=%u rounds=%llu left_ms=%llu\nL2 %s %s row=%u rounds=%llu left_ms=%llu\nNETWORK WiFi=%s SSID=%s ip=%s BLE=%s secure=%u\nRECOVERY enabled=%u count_outage=%u threshold_s=%lu checkpoint_s=%lu waiting=%u gap_ms=%llu",
    clock,clockSourceName(clockSource),armed,MAX_ACTIVE_CHANNELS,logicPassed?"PASS":"FAIL",storageReady,
    temperature,humidity,current.valid,output[0]?"ON":"OFF",modeName(settings.config[0].mode),progress[0].row+1,progress[0].completed,progress[0].remainingMs,
    output[1]?"ON":"OFF",modeName(settings.config[1].mode),progress[1].row+1,progress[1].completed,progress[1].remainingMs,
    net.connected?"CONNECTED":"DISCONNECTED",net.ssid,net.ip,ble.connected?"CONNECTED":ble.advertisingPaused?"PAUSED_AP":ble.ready?"READY":"DISABLED",ble.secure,
    cycleRecoveryEnabled(),policySettings.channel[0].cycle.countOutage,static_cast<unsigned long>(settings.recovery.restartAfterSeconds),static_cast<unsigned long>(settings.recovery.checkpointSeconds),resumeWaiting,recoveryGapMs);
  size_t usedNAS=strlen(b);if(usedNAS<size)snprintf(b+usedNAS,size-usedNAS,"\nNAS enabled=%u waiting=%u source=%s outage_ms=%llu startup_ms=%llu",nasRecoveryEnabled,nasWaiting&&resumeWaiting,recoverySource,estimatedOutageMs,startupWaitMs);
  auto m=mqttView();auto d=eventLogView();size_t used=strlen(b);if(used<size)snprintf(b+used,size-used,"\nMQTT enabled=%u connected=%u host=%s:%u\nLOG ready=%u boot=%lu reason=%s",m.enabled,m.connected,m.host,m.port,d.ready,static_cast<unsigned long>(d.bootCount),d.resetReason);
}
__attribute__((noinline)) bool diagnostics(char*reply,size_t size){auto d=eventLogView();auto m=mqttView();auto n=networkView();snprintf(reply,size,"DIAG boot_id=%s reason=%s uptime_ms=%llu heap_free=%lu heap_min=%lu heap_largest=%lu controller_stack=%lu log_stack=%lu loop_stack=%lu\nLOG ready=%u pending=%u dropped=%lu write_failures=%lu boot_count=%lu power_or_external=%lu known_wdt=%lu\nMQTT enabled=%u connected=%u host=%s port=%u error=%d retained_rejected=%lu\nWEB http_stack=%lu requests=%lu command_timeouts=%lu write_failures=%lu",d.bootId,d.resetReason,monoMs(),static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMinFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()),static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),static_cast<unsigned long>(d.stackMinimum),static_cast<unsigned long>(loopTask?uxTaskGetStackHighWaterMark(loopTask):0),d.ready,d.pending,static_cast<unsigned long>(d.dropped),static_cast<unsigned long>(d.writeFailures),static_cast<unsigned long>(d.bootCount),static_cast<unsigned long>(d.powerLikeStarts),static_cast<unsigned long>(d.watchdogStarts),m.enabled,m.connected,m.host,m.port,m.lastError,static_cast<unsigned long>(m.retainedCommandsRejected),static_cast<unsigned long>(n.httpStackMinimum),static_cast<unsigned long>(n.stateRequests),static_cast<unsigned long>(n.commandTimeouts),static_cast<unsigned long>(n.responseWriteFailures));if(strlen(reply)+150<size)snprintf(reply+strlen(reply),size-strlen(reply),"\nWEB_HOME requests=%lu failures=%lu last_ms=%lu sent=%lu io_failure=%u errno=%d",static_cast<unsigned long>(n.homeRequests),static_cast<unsigned long>(n.homeFailures),static_cast<unsigned long>(n.homeLastMs),static_cast<unsigned long>(n.homeLastSent),n.httpLastFailure,n.httpLastErrno);if(strlen(reply)+70<size)snprintf(reply+strlen(reply),size-strlen(reply),"\nLOG_IO stage=%s errno=%d at_ms=%lu age_ms=%lu",d.lastFailure[0]?d.lastFailure:"none",d.lastError,static_cast<unsigned long>(d.lastFailureAtMs),static_cast<unsigned long>(d.lastFailure[0]?uint32_t(millis()-d.lastFailureAtMs):0));return true;}
__attribute__((noinline)) bool remoteNetworkInfo(bool mqtt,char*reply,size_t size){
 if(mqtt){auto m=mqttView();snprintf(reply,size,"MQTT enabled=%u connected=%u host=%s port=%u error=%d errno=%d attempts=%lu outbox=%lu heap_free=%lu heap_largest=%lu",m.enabled,m.connected,m.host,m.port,m.lastError,m.socketError,static_cast<unsigned long>(m.connectionAttempts),static_cast<unsigned long>(m.outboxBytes),static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()));}
 else{auto n=networkView();snprintf(reply,size,"WIFI connected=%u SSID=%s ip=%s AP=%u ap_ip=%s rssi=%d generation=%lu disconnect=%lu http_restarts=%lu",n.connected,n.ssid,n.ip,n.apActive,n.apIp,n.rssi,static_cast<unsigned long>(n.generation),static_cast<unsigned long>(n.disconnectReason),static_cast<unsigned long>(n.httpRestarts));}
 return true;
}
__attribute__((noinline)) void opticalStatus(const char *message){PhotoAutoView a={};a.active=optical.active;a.completed=optical.index;a.total=optical.total;const auto point=autocal::point(optical.mask,optical.index);a.channel=point.channel+1;a.stage=point.dark?1:point.full?2:3;a.frequency=point.dark||point.full?0:photomodel::frequency[point.frequencyIndex];a.duty=point.dark?0:point.full?100:photomodel::duty[point.dutyIndex];snprintf(a.message,sizeof(a.message),"%s",message);photoAutoProgress(a);}
void opticalStage(){optical.paused=false;const auto point=autocal::point(optical.mask,optical.index);for(auto &c:opticalOutput)c=program::defaults();if(!point.dark&&!point.full&&!point.done){auto&c=opticalOutput[point.channel];c.pwm=true;c.frequency=photomodel::frequency[point.frequencyIndex];c.duty=photomodel::duty[point.dutyIndex];}if(!point.dark&&!point.full&&!point.done&&!configurePwm(point.channel)){opticalCancel("PWM calibration frequency failed");return;}optical.stageAt=monoMs();optical.lastSample=0;optical.count=0;optical.sum[0]=optical.sum[1]=0;currentDuty[0]=currentDuty[1]=255;opticalStatus("Waiting for sensor settling and averaging");}
void opticalCancel(const char *reason){optical.active=false;optical.paused=false;dailyPendingMask=0;armed=false;enabled[0]=enabled[1]=false;for(auto&r:runtime)r={};for(auto &c:opticalOutput)c=program::defaults();for(unsigned i=0;i<2;++i){drive(i,false);if(!configurePwm(i))healthLatch(FAULT_PWM);}currentDuty[0]=currentDuty[1]=255;opticalStatus(reason);checkpointDirty=true;}
bool opticalStart(uint8_t mask,uint32_t settle,uint32_t sample,char*reply,size_t size){if(!INC_ENABLE_PHOTO||!photoView().compiled||!photoView().sampledMs){snprintf(reply,size,"ERR light detector unavailable");return false;}if(!mqttNasAlive()||eventLogView().pending>8){snprintf(reply,size,"ERR wait for NAS handshake and event backlog below9");return false;}if(!netbudget::calibrationReady(ESP.getFreeHeap(),ESP.getMaxAllocHeap())){snprintf(reply,size,"ERR calibration memory: need free>=16384 and largest>=4096; close device web pages and retry");return false;}if(armed||resumeWaiting||optical.active){snprintf(reply,size,"ERR stop both channels before automatic calibration");return false;}if(!logicPassed||!storageReady){snprintf(reply,size,"ERR controller/configuration unavailable");return false;}if(mask<1||mask>3||settle<1||settle>30||sample<1||sample>30){snprintf(reply,size,"ERR mask1..3 settle/sample1..30 seconds");return false;}dailyPendingMask=0;enabled[0]=enabled[1]=false;for(auto&r:runtime)r={};if(!saveManual()||!checkpoint()){snprintf(reply,size,"ERR stop snapshot write failed");return false;}optical={};optical.mask=mask;optical.total=autocal::pointCount(mask);optical.settleMs=settle*1000;optical.sampleMs=sample*1000;snprintf(optical.id,sizeof(optical.id),"%06lx-c%08lx",static_cast<unsigned long>(deviceIdentityValue()),static_cast<unsigned long>(esp_random()));optical.active=true;opticalStage();if(!optical.active){snprintf(reply,size,"ERR calibration PWM initialization");return false;}snprintf(reply,size,"OK automatic calibration started; both experiments paused, finish locked");return true;}
__attribute__((noinline)) void opticalTick(){if(!optical.active)return;const bool memoryLow=netbudget::calibrationLow(ESP.getFreeHeap(),ESP.getMaxAllocHeap());const auto logs=eventLogView();if(!logicPassed||!storageReady||!logs.ready){opticalCancel("Controller or Flash storage unavailable");return;}if(memoryLow||logs.pending>=48){if(!optical.paused){optical.paused=true;drive(0,false);drive(1,false);opticalStatus(memoryLow?"Paused: RAM low; Flash points retained":"Paused: Flash backlog; waiting for upload");}return;}if(optical.paused){if(!netbudget::calibrationReady(ESP.getFreeHeap(),ESP.getMaxAllocHeap())||logs.pending>8)return;opticalStage();return;}const uint64_t now=monoMs();const auto point=autocal::point(optical.mask,optical.index);const auto p=photoView();if(!p.compiled||!p.sampledMs||now-p.sampledMs>2000){opticalCancel("ADC sampling timeout");return;}if(now-optical.stageAt<optical.settleMs)return;if(p.sampledMs>=optical.stageAt+optical.settleMs&&p.sampledMs!=optical.lastSample){optical.lastSample=p.sampledMs;optical.sum[0]+=p.channels[0].raw;optical.sum[1]+=p.channels[1].raw;++optical.count;}if(now-optical.stageAt<optical.settleMs+optical.sampleMs)return;if(optical.count<8){if(now-optical.stageAt>optical.settleMs+optical.sampleMs+3000)opticalCancel("Insufficient independent ADC samples");return;}if(!eventCalibrationPoint(optical.id,point.channel,point.dark||point.full?0:photomodel::frequency[point.frequencyIndex],point.dark?0:point.full?100:photomodel::duty[point.dutyIndex],p.channels[point.channel].raw,p.channels[1-point.channel].raw,controllerEventSnapshot(),optical.sum[point.channel],optical.sum[1-point.channel],optical.count)){optical.paused=true;drive(0,false);drive(1,false);opticalStatus("Paused: point queue busy; current point will be resampled");return;}++optical.index;if(autocal::point(optical.mask,optical.index).done){opticalCancel("Raw points saved to Flash; NAS fits after upload");return;}opticalStage();}
__attribute__((noinline)) bool execute(char *line,char *reply,size_t size) {
  char *tokens[6],*context=nullptr;uint8_t count=0;
  for(char *p=strtok_r(line," \t",&context);p;p=strtok_r(nullptr," \t",&context)) {if(count==6){snprintf(reply,size,"ERR too many arguments");return false;}tokens[count++]=p;}
  auto fail=[&](const char *m){snprintf(reply,size,"ERR %s",m);return false;};
  auto ok=[&](const char *m){snprintf(reply,size,"OK %s",m);return true;};
  if(!count)return fail("empty command");const char *name=tokens[0];
  if(!strcmp(name,"photocal")&&count==2&&!strcmp(tokens[1],"cancel")){opticalCancel("Cancelled by user");applyOutputs();return ok("calibration cancelled; both lights off");}
  if(!strcmp(name,"photocal")&&count==5&&!strcmp(tokens[1],"start")){uint32_t mask,settle,sample;if(!number(tokens[2],3,mask)||!number(tokens[3],30,settle)||!number(tokens[4],30,sample))return fail("photocal start MASK SETTLE_SECONDS SAMPLE_SECONDS");return opticalStart(mask,settle,sample,reply,size);}
  if(optical.active&&strcmp(name,"status")&&strcmp(name,"diag")&&strcmp(name,"errors")&&strcmp(name,"disarm"))return fail("automatic calibration active; cancel before other changes");
  if(!strcmp(name,"status")&&count==1){status(reply,size);return true;}
  if(!strcmp(name,"diag")&&count==1)return diagnostics(reply,size);
  if(count==2&&!strcmp(tokens[1],"info")&&(!strcmp(name,"wifi")||!strcmp(name,"mqtt")))return remoteNetworkInfo(!strcmp(name,"mqtt"),reply,size);
  if(!strcmp(name,"wifi")&&count==2&&!strcmp(tokens[1],"restart")){if(armed)return fail("lock before restarting network");return networkRequestAction(NetworkAction::Restart)?ok("network restart queued"):fail("network action pending");}
  if(!strcmp(name,"mqtt")&&count==2&&(!strcmp(tokens[1],"on")||!strcmp(tokens[1],"off"))){if(armed)return fail("lock before changing MQTT");auto m=mqttView();if(!mqttConfigure(!strcmp(tokens[1],"on"),m.host,m.port,m.username,"",reply,size))return false;return ok("MQTT configuration saved; asynchronous apply");}
  if(!strcmp(name,"errors")&&count==1){auto h=healthView();snprintf(reply,size,"HEALTH active=0x%lx latched=0x%lx rejected=%lu last=%s",static_cast<unsigned long>(h.active),static_cast<unsigned long>(h.latched),static_cast<unsigned long>(h.commandErrors),h.lastCommandError);return true;}
  if(!strcmp(name,"errors")&&count==2&&!strcmp(tokens[1],"clear")){if(armed)return fail("lock before clearing errors");healthClear();return ok("historical errors cleared; active faults remain visible");}
  if((!strcmp(name,"manualresume")||!strcmp(name,"nasresume"))&&(count==2||count==3)){if(resumeWaiting)return fail("wait for boot recovery decision");uint32_t ch=0;const char*op=tokens[count-1];if(count==3&&(!number(tokens[1],2,ch)||!ch))return fail("channel1|2");if(strcmp(op,"on")&&strcmp(op,"off"))return fail("on|off required");auto old=policySettings;for(unsigned i=0;i<2;++i)if(!ch||i==ch-1){if(!strcmp(name,"manualresume"))policySettings.channel[i].manualEnabled=!strcmp(op,"on");else policySettings.channel[i].nasEnabled=!strcmp(op,"on");}if(!savePolicies()){policySettings=old;return fail("recovery preference write failed");}if(!saveManual())return fail("manual snapshot write failed");checkpointDirty=true;return ok("channel recovery preference saved; other output kept");}
  if(!strcmp(name,"dailyresume")&&count==3){uint32_t ch;if(!number(tokens[1],2,ch)||!ch||(strcmp(tokens[2],"on")&&strcmp(tokens[2],"off")))return fail("dailyresume CHANNEL on|off");const auto before=dailyPreferences;const uint8_t bit=1U<<(ch-1);if(!strcmp(tokens[2],"on"))dailyPreferences.mask|=bit;else dailyPreferences.mask&=~bit;if(!saveDailyPreferences()){dailyPreferences=before;healthLatch(FAULT_NVS);return fail("daily recovery preference write failed");}if(!strcmp(tokens[2],"off"))dailyPendingMask&=~bit;return ok("daily reboot preference saved; current channel and other progress kept");}
  if(!strcmp(name,"recovery")&&count==2&&!strcmp(tokens[1],"info")){size_t used=0;for(unsigned i=0;i<2;++i){const auto&p=policySettings.channel[i];int n=snprintf(reply+used,size-used,"L%u enabled=%u manual_resume=%u daily_resume=%u daily_waiting=%u cycle_resume=%u NAS=%u count_outage=%u restart_s=%lu checkpoint_s=%lu source=%s gap_ms=%llu outage_ms=%llu startup_ms=%llu\n",i+1,armed&&enabled[i],p.manualEnabled,bool(dailyPreferences.mask&(1U<<i)),bool(dailyPendingMask&(1U<<i)),p.cycle.enabled,p.nasEnabled,p.cycle.countOutage,static_cast<unsigned long>(p.cycle.restartAfterSeconds),static_cast<unsigned long>(p.cycle.checkpointSeconds),perSource[i],perGap[i],perOutage[i],perStartup[i]);if(n<0||size_t(n)>=size-used)return fail("recovery response too long");used+=n;}return true;}
  if(!strcmp(name,"pwm")&&count==2&&!strcmp(tokens[1],"info")){snprintf(reply,size,"PWM engine=LEDC XTAL40MHz resolution=10bit L1 requested=%u actual=%lu duty_ticks=%lu high_ns=%llu L2 requested=%u actual=%lu duty_ticks=%lu high_ns=%llu",settings.config[0].frequency,static_cast<unsigned long>(pwmReady[0]?ledc_get_freq(LEDC_LOW_SPEED_MODE,LEDC_TIMER_0):0),static_cast<unsigned long>((1024UL*settings.config[0].duty+50)/100),((1024ULL*settings.config[0].duty+50)/100)*1000000000ULL/(1024ULL*settings.config[0].frequency),settings.config[1].frequency,static_cast<unsigned long>(pwmReady[1]?ledc_get_freq(LEDC_LOW_SPEED_MODE,LEDC_TIMER_1):0),static_cast<unsigned long>((1024UL*settings.config[1].duty+50)/100),((1024ULL*settings.config[1].duty+50)/100)*1000000000ULL/(1024ULL*settings.config[1].frequency));return true;}
  if(!strcmp(name,"photo")&&count==4&&!strcmp(tokens[2],"dim")){uint32_t ch,value;if(!number(tokens[1],2,ch)||!ch||!number(tokens[3],1000,value)||!value)return fail("dim ratio permille1..1000");return photoDimThreshold(ch-1,value,reply,size);}
  if(!strcmp(name,"photo")&&count==5&&!strcmp(tokens[2],"thresholds")){uint32_t ch,on,off;if(!number(tokens[1],2,ch)||!ch||!number(tokens[3],1000,on)||!number(tokens[4],1000,off))return fail("photo CHANNEL thresholds ON_MIN OFF_MAX (permille)");return photoThresholds(ch-1,on,off,reply,size);}
  if(!strcmp(name,"photo")&&count==3){uint32_t ch;if(!number(tokens[1],2,ch)||!ch)return fail("photo channel1|2");if(!strcmp(tokens[2],"dark")&&(output[0]||output[1]))return fail("dark capture requires both outputs OFF");if(!strcmp(tokens[2],"bright")&&(settings.config[ch-1].pwm||!output[ch-1]||output[2-ch]))return fail("full-bright capture requires this light ON with PWM OFF, other light OFF");return photoConfigure(ch-1,tokens[2],reply,size);}
  if(!strcmp(name,"logs")&&count==1){eventLogPrint();return ok("recent diagnostic log requested on USB");}
  if(!strcmp(name,"logs")&&count==2&&!strcmp(tokens[1],"init")){if(armed)return fail("lock before initializing log filesystem");eventLogInitialize();return ok("log filesystem initialization requested; see LOG_STORAGE");}
  if((!strcmp(name,"arm")||!strcmp(name,"disarm"))&&count==2){
 uint32_t ch;if(!number(tokens[1],2,ch)||!ch)return fail("channel1|2");--ch;
 if(!strcmp(name,"arm")){if(resumeWaiting)return fail("wait for boot recovery decision");if(!logicPassed)return fail("logic self-test failed");tm t={};if(policySettings.channel[ch].cycle.enabled&&settings.config[ch].mode==program::Mode::Cycle&&!clockOk(t))return fail("set clock before cycle start");dailyPendingMask=dailyrestart::cancel(dailyPendingMask,ch);if(!enabled[ch]||progress[ch].finished){runtime[ch]={false,true,monoMs(),0};enabled[ch]=true;finishedReported[ch]=false;}armed=true;}
 else{dailyPendingMask=dailyrestart::cancel(dailyPendingMask,ch);recoveryMask&=~(1U<<ch);resumeWaiting=bool(recoveryMask);if(!resumeWaiting)nasWaiting=false;enabled[ch]=false;runtime[ch]={};armed=enabled[0]||enabled[1];}
 checkpointDirty=true;applyOutputs();if(!saveManual())return fail("manual snapshot write failed");if(!resumeWaiting&&!checkpoint())return fail("checkpoint write failed");return ok("channel permission changed; other progress kept");
 }
  if(!strcmp(name,"disarm")&&count==1){if(optical.active)opticalCancel("Stopped and locked by user");armed=false;resumeWaiting=false;recoveryMask=0;dailyPendingMask=0;manualPending=false;enabled[0]=enabled[1]=false;for(auto &r:runtime){r.manualOn=false;r.running=false;}applyOutputs();checkpointDirty=true;
    if(pwmSettingsDirty&&!saveSettings())return fail("LOCKED but PWM settings write failed");
    if(!saveManual())return fail("LOCKED but manual state write failed");
    if(cycleRecoveryEnabled()&&!checkpoint())return fail("LOCKED but recovery record write failed");publish();eventEmit("stop","LOCKED",controllerEventSnapshot(),true);return ok("LOCKED; settings kept");}
  if(!strcmp(name,"arm")&&count==1){
    if(!logicPassed)return fail("logic self-test failed");
    if(pwmSettingsDirty&&!saveSettings())return fail("PWM settings write failed; remains LOCKED");
    tm t={};for(unsigned i=0;i<2;++i)if(settings.config[i].mode==program::Mode::Cycle&&policySettings.channel[i].cycle.enabled&&!clockOk(t))return fail("set clock before starting a recoverable cycle");
    if(resumeWaiting)return fail("wait for boot recovery decision");
    manualPending=false;dailyPendingMask=0;
    for(uint8_t i=0;i<2;++i)if(!enabled[i]||progress[i].finished){runtime[i]={false,true,monoMs(),0};enabled[i]=true;finishedReported[i]=false;}armed=true;checkpointDirty=true;
    if(!checkpoint()){armed=false;enabled[0]=enabled[1]=false;applyOutputs();return fail("checkpoint write failed; LOCKED");}
    applyOutputs();if(!saveManual()){armed=false;applyOutputs();return fail("manual state write failed; LOCKED");}return ok("ARMED; stopped channels start first row; running progress kept");}
  if(!strcmp(name,"off")&&count==2&&!strcmp(tokens[1],"all")){dailyPendingMask=0;
    settingsBeforeEdit=settings;for(uint8_t i=0;i<2;++i){settings.config[i].mode=program::Mode::Manual;runtime[i].manualOn=false;runtime[i].running=false;}++settings.revision;
    applyOutputs();checkpointDirty=true;if(!saveSettings()){settings=settingsBeforeEdit;armed=false;applyOutputs();return fail("outputs locked; settings write failed");}if(!checkpoint())return fail("checkpoint write failed");return ok("all OFF MANUAL");}
  if(!strcmp(name,"clock")&&count==3){if(armed)return fail("lock before setting clock");if(!setClock(tokens[1],tokens[2]))return fail("invalid date/time");return ok("clock UTC+8 MANUAL");}
  if(!strcmp(name,"resume")&&(count==5||count==6)){if(resumeWaiting)return fail("wait for boot recovery decision");const uint8_t offset=count==6?1:0;uint32_t ch=0,on,threshold,interval;if(offset&&(!number(tokens[1],2,ch)||!ch))return fail("channel1|2");if(!number(tokens[1+offset],1,on)||!number(tokens[3+offset],604800,threshold)||!number(tokens[4+offset],3600,interval)||interval<10||(strcmp(tokens[2+offset],"advance")&&strcmp(tokens[2+offset],"pause")))return fail("resume [CHANNEL] 0|1 advance|pause threshold save_interval");auto old=policySettings;for(unsigned i=0;i<2;++i)if(!ch||i==ch-1)policySettings.channel[i].cycle={bool(on),!strcmp(tokens[2+offset],"advance"),threshold,interval};if(!savePolicies()){policySettings=old;return fail("policy write failed");}checkpointDirty=true;return ok("channel cycle recovery saved; other progress kept");}
  if(!strcmp(name,"test")&&count==1){if(armed)return fail("lock before test");logicPassed=program::selfTest();return logicPassed?ok("LOGIC PASS"):fail("LOGIC FAIL");}
  uint32_t index;if(count<2||!number(tokens[1],2,index)||!index)return fail("unknown command or channel");uint8_t ch=index-1;
  if(!strcmp(name,"show")&&count==2){
    const auto &c=settings.config[ch];String s="CONFIG ";s+=String(index)+" daily=";
    char pair[32];for(uint8_t i=0;i<c.dailyCount;++i){snprintf(pair,sizeof(pair),"%02lu:%02lu:%02lu-%02lu:%02lu:%02lu",c.daily[i].on/3600,(c.daily[i].on/60)%60,c.daily[i].on%60,c.daily[i].off/3600,(c.daily[i].off/60)%60,c.daily[i].off%60);if(i)s+=',';s+=pair;}
    s+=" cycle=";s+=String(c.repeats)+" ";for(uint8_t i=0;i<c.stepCount;++i){char on[20],off[20];decimal::seconds(on,sizeof(on),c.steps[i].onMs);decimal::seconds(off,sizeof(off),c.steps[i].offMs);if(i)s+=',';s+=String(on)+"/"+off;}s+=" pwm=";s+=c.pwm?String(c.frequency)+"Hz "+String(c.duty)+"%":"off";
    snprintf(reply,size,"%s",s.c_str());return true;
  }
  if((!strcmp(name,"on")||!strcmp(name,"off"))&&count==2){
    bool on=!strcmp(name,"on");if(resumeWaiting)return fail("wait for boot recovery decision");if(on&&(!armed||!enabled[ch]))return fail("channel locked; use arm CHANNEL");
    if(on&&MAX_ACTIVE_CHANNELS==1&&(output[1-ch]||progress[1-ch].requested))return fail("stop other channel first");
    settingsBeforeEdit=settings;bool changed=settings.config[ch].mode!=program::Mode::Manual;settings.config[ch].mode=program::Mode::Manual;
    dailyPendingMask=dailyrestart::cancel(dailyPendingMask,ch);runtime[ch].manualOn=on;runtime[ch].running=false;
    if(changed){++settings.revision;if(!saveSettings()){settings=settingsBeforeEdit;runtime[ch].manualOn=false;armed=false;applyOutputs();return fail("settings write failed; LOCKED");}}
    checkpointDirty=true;applyOutputs();if(!saveManual()){armed=false;applyOutputs();return fail("manual state write failed; LOCKED");}if(!checkpoint())return fail("checkpoint write failed");return ok("MANUAL");}
  if((!strcmp(name,"pwm")&&(count==3||count==4))||(!strcmp(name,"pwmset")&&count==4)){
    settingsBeforeEdit=settings;const auto&old=settingsBeforeEdit.config[ch];editCandidate=old;auto&proposed=editCandidate;
    if(!strcmp(name,"pwmset")){
      uint32_t value;if(!strcmp(tokens[2],"enable")){if(strcmp(tokens[3],"ON")&&strcmp(tokens[3],"OFF"))return fail("PWM enable ON|OFF");proposed.pwm=!strcmp(tokens[3],"ON");}
      else if(!strcmp(tokens[2],"duty")){if(!number(tokens[3],99,value)||!value)return fail("PWM duty1..99");proposed.duty=value;}
      else if(!strcmp(tokens[2],"frequency")){if(!number(tokens[3],20000,value)||value<40)return fail("PWM frequency40..20000");proposed.frequency=value;}
      else return fail("PWM parameter enable|duty|frequency");
    }
    else if(count==3&&!strcmp(tokens[2],"off"))proposed.pwm=false;
    else{uint32_t hz,duty;if(count!=4||!number(tokens[2],20000,hz)||hz<40||!number(tokens[3],99,duty)||!duty)return fail("pwm CHANNEL HZ(40..20000) DUTY(1..99), or pwm CHANNEL off");proposed.pwm=true;proposed.frequency=hz;proposed.duty=duty;}
    if(!storageReady)return fail("configuration storage unavailable");
    settings.config[ch]=proposed;
    bool timerOk=true;esp_err_t frequencyResult=ESP_OK;uint32_t actualFrequency=0;
    if(proposed.pwm){if(old.pwm&&pwmReady[ch]){frequencyResult=ledc_set_freq(LEDC_LOW_SPEED_MODE,static_cast<ledc_timer_t>(ch),proposed.frequency);actualFrequency=ledc_get_freq(LEDC_LOW_SPEED_MODE,static_cast<ledc_timer_t>(ch));timerOk=pwmcheck::accepted(frequencyResult,actualFrequency,proposed.frequency);}else{timerOk=configurePwm(ch);actualFrequency=ledc_get_freq(LEDC_LOW_SPEED_MODE,static_cast<ledc_timer_t>(ch));frequencyResult=timerOk?ESP_OK:ESP_FAIL;timerOk=timerOk&&pwmcheck::accepted(frequencyResult,actualFrequency,proposed.frequency);}}
    if(!timerOk){healthLatch(FAULT_PWM);settings.config[ch]=old;if(old.pwm&&!configurePwm(ch)){armed=false;applyOutputs();}snprintf(reply,size,"ERR PWM frequency: %s (%d); requested=%lu actual=%lu",esp_err_to_name(frequencyResult),frequencyResult,static_cast<unsigned long>(proposed.frequency),static_cast<unsigned long>(actualFrequency));return false;}
    currentDuty[ch]=255;pwmSettingsDirty=true;pwmChangedAt=monoMs();applyOutputs();publish();eventEmit("pwm",ch==0?"light1":"light2",controllerEventSnapshot(),true);return ok("PWM applied; progress kept; save after 2s");
  }
  if(resumeWaiting)return fail("wait for boot recovery decision");manualPending=false;
  const bool channelWasActive=armed&&enabled[ch];
  editCandidate=settings.config[ch];auto&proposed=editCandidate;char *rowContext=nullptr;
  if(!strcmp(name,"daily")&&count==3){
    proposed.dailyCount=0;
    for(char *row=strtok_r(tokens[2],",",&rowContext);row;row=strtok_r(nullptr,",",&rowContext)){
      char *dash=strchr(row,'-');if(!dash||proposed.dailyCount==program::MAX_ROWS)return fail("daily rows: max 16; HH:MM:SS-HH:MM:SS");*dash=0;
      auto &w=proposed.daily[proposed.dailyCount++];if(!daySecond(row,w.on)||!daySecond(dash+1,w.off))return fail("invalid daily time");
    }
    proposed.mode=program::Mode::Daily;
  }else if(!strcmp(name,"cycle")&&count==4){
    if(!number(tokens[2],program::MAX_REPEATS,proposed.repeats))return fail("repeat count 0..1000000 (0 infinite)");proposed.stepCount=0;
    for(char *row=strtok_r(tokens[3],",",&rowContext);row;row=strtok_r(nullptr,",",&rowContext)){
      char *slash=strchr(row,'/');if(!slash||proposed.stepCount==program::MAX_ROWS)return fail("cycle rows: max 16; ON/OFF in seconds");*slash=0;
      auto &s=proposed.steps[proposed.stepCount++];if(!seconds(row,s.onMs)||!seconds(slash+1,s.offMs))return fail("phase seconds 0.1..604800, one decimal place");
    }proposed.mode=program::Mode::Cycle;
  }else return fail("syntax; use status/show/daily/cycle/pwm/resume");
  if(!program::valid(proposed))return fail("invalid or overlapping rows");
  settingsBeforeEdit=settings;settings.config[ch]=proposed;++settings.revision;
  if(!configurePwm(ch)){settings=settingsBeforeEdit;configurePwm(ch);return fail("PWM timer configuration");}
  if(!saveSettings()){settings=settingsBeforeEdit;configurePwm(ch);return fail("settings write failed");}
  dailyPendingMask=dailyrestart::cancel(dailyPendingMask,ch);runtime[ch]={false,channelWasActive,monoMs(),0};finishedReported[ch]=false;enabled[ch]=channelWasActive;checkpointDirty=true;
  applyOutputs();if(!checkpoint())return fail("checkpoint write failed");return ok(channelWasActive?"program saved; this channel starts first row; other progress kept":"program saved; this channel remains stopped; other progress kept");
}
void controllerTask(void *) {
  TickType_t wake=xTaskGetTickCount();uint32_t lastPublish=0;
  for(;;){
    restoreManual();if(!resumeWaiting)nasWaiting=false;
    if(networkTakeTimeSync()&&!mqttNasAlive()){tm t={};time_t epoch=time(nullptr);if(localtime_r(&epoch,&t)&&t.tm_year>=124&&t.tm_year<=199){bool first=clockSource!=ClockSource::Ntp;clockSource=ClockSource::Ntp;if(first&&journal.epochMs&&epochMs()>=journal.epochMs)recoveryGapMs=epochMs()-journal.epochMs;recover();publish();if(first)eventEmit("time_sync","NTP",controllerEventSnapshot(),true);usb("TIME SYNC NTP\n");}}
    portENTER_CRITICAL(&stateMux);const uint64_t nasOffset=nasClockOffset;nasClockOffset=0;portEXIT_CRITICAL(&stateMux);
    if(nasOffset){const uint64_t ms=nasOffset+monoMs();timeval tv={};tv.tv_sec=ms/1000;tv.tv_usec=(ms%1000)*1000;if(!settimeofday(&tv,nullptr)){const bool first=clockSource!=ClockSource::Nas;clockSource=ClockSource::Nas;if(first){if(journal.epochMs&&ms>=journal.epochMs)recoveryGapMs=ms-journal.epochMs;recover();publish();eventEmit("time_sync","NAS",controllerEventSnapshot(),true);usb("TIME SYNC NAS\n");}}}
    static ControlCommand request={};static ControlReply reply={};
    if(xQueueReceive(commands,&request,0)==pdTRUE){reply={};reply.id=request.id;
      if(monoMs()>=request.expiresMs){reply.ok=false;snprintf(reply.text,sizeof(reply.text),"ERR expired; no change applied");}
      else if(request.origin==Origin::Recovery)reply.ok=acceptRecovery(request.text,reply.text,sizeof(reply.text));
      else reply.ok=execute(request.text,reply.text,sizeof(reply.text));
      if(!reply.ok)healthCommandError(reply.text);applyOutputs();publish();
      if(request.origin==Origin::Web)networkAnswer(reply);else if(request.origin==Origin::Ble)bleAnswer(reply);else if(request.origin==Origin::Mqtt||request.origin==Origin::Recovery)mqttAnswer(reply);else usb("%s\n",reply.text);
      if(request.origin==Origin::Mqtt)eventEmit("command_result",reply.ok?"MQTT_OK":"MQTT_REJECTED",controllerEventSnapshot(),true);
    }
    recover();restoreDaily();opticalTick();applyOutputs();
    if(pwmSettingsDirty&&monoMs()-pwmChangedAt>=2000&&!saveSettings()){armed=false;pwmSettingsDirty=false;healthLatch(FAULT_NVS);applyOutputs();usb("ERR PWM settings persistence failed; LOCKED\n");}
    if(checkpointDirty||(cycleRecoveryEnabled()&&armed&&monoMs()-lastCheckpointMono>=static_cast<uint64_t>(channelpolicy::checkpointInterval(policySettings.channel,enabled))*1000)){
      if(!resumeWaiting && (cycleRecoveryEnabled() || checkpointDirty)){
        if(!checkpoint()){healthLatch(FAULT_NVS);checkpointDirty=false;lastCheckpointMono=monoMs();if(cycleRecoveryEnabled()){armed=false;applyOutputs();usb("ERR checkpoint failed; LOCKED\n");}}
      }
    }
    if(static_cast<uint32_t>(millis()-lastPublish)>=100){lastPublish=millis();publish();}
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
  }
}
bool submit(const char *text,Origin origin) {ControlCommand c={};c.origin=origin;c.expiresMs=monoMs()+2000;snprintf(c.text,sizeof(c.text),"%s",text);return controlEnqueue(c);}
void serviceSerial() {
  static char line[sizeof(ControlCommand::text)];static size_t used=0;static bool overflow=false;uint8_t budget=64;
  while(Serial.available()&&budget--){char c=Serial.read();if(c=='\r')continue;
    if(c=='\n'){
      line[used]=0;
      if(overflow)usb("ERR line too long\n");
      else if(!strcmp(line,"wifi restart")){if(controlSnapshot().armed)usb("ERR lock before WiFi restart\n");else networkRequestAction(NetworkAction::Restart);}
      else if(!strcmp(line,"wifi info"))networkRequestAction(NetworkAction::Info);
      else if(!strcmp(line,"wifi radio"))networkRequestAction(NetworkAction::RadioInfo);
      else if(!strcmp(line,"wifi sleep off")||!strcmp(line,"wifi sleep on")){if(controlSnapshot().armed)usb("ERR lock before WiFi sleep setting\n");else if(!networkRequestAction(!strcmp(line,"wifi sleep on")?NetworkAction::SleepOn:NetworkAction::SleepOff))usb("ERR network action queue busy\n");}
      else if(!strcmp(line,"wifi key"))networkRequestAction(NetworkAction::Secrets);
      else if(!strcmp(line,"samples info")){sampleJournalPrint();}
      else if(!strcmp(line,"mqtt info")){auto m=mqttView();usb("MQTT enabled=%u connected=%u host=%s port=%u username=%s error=%d type=%d tls=%d errno=%d attempts=%lu outbox=%lu\nMQTT_MEMORY inflight=%lu failures=%lu deferred=%lu stack=%lu before=%lu init=%lu start=%lu connect=%lu\n",m.enabled,m.connected,m.host,m.port,m.username,m.lastError,m.errorType,m.transportError,m.socketError,static_cast<unsigned long>(m.connectionAttempts),static_cast<unsigned long>(m.outboxBytes),static_cast<unsigned long>(m.inflightId),static_cast<unsigned long>(m.publishFailures),static_cast<unsigned long>(m.memoryDeferrals),static_cast<unsigned long>(m.stackMinimum),static_cast<unsigned long>(m.heapBefore),static_cast<unsigned long>(m.heapAfterInit),static_cast<unsigned long>(m.heapAfterStart),static_cast<unsigned long>(m.heapAfterConnect));usb("MQTT_WAIT reason=%s retry_ms=%lu heap=%lu/%lu largest=%lu/%lu\n",m.waitReason[0]?m.waitReason:"none",static_cast<unsigned long>(m.retryMs),static_cast<unsigned long>(m.currentHeap),static_cast<unsigned long>(m.waitHeap),static_cast<unsigned long>(m.currentLargest),static_cast<unsigned long>(m.waitLargest));const auto life=mqttLifecycleView();const auto io=rawmqtt::failureView();usb("MQTT_LIFECYCLE creations=%lu memory_stops=%lu ack_timeout_stops=%lu last_stop=%s\nMQTT_IO failures=%lu stage=%s errno=%d fd=%d at_ms=%lu elapsed_ms=%lu requested=%lu sent=%lu rssi=%d heap=%lu largest=%lu\n",static_cast<unsigned long>(life.creations),static_cast<unsigned long>(life.memoryStops),static_cast<unsigned long>(life.ackTimeoutStops),life.lastStop,static_cast<unsigned long>(io.count),io.stage[0]?io.stage:"none",io.error,io.fd,static_cast<unsigned long>(io.atMs),static_cast<unsigned long>(io.elapsedMs),static_cast<unsigned long>(io.requested),static_cast<unsigned long>(io.sent),io.rssi,static_cast<unsigned long>(io.heap),static_cast<unsigned long>(io.largest));}
      else if(!strcmp(line,"mqtt probe")){if(controlSnapshot().armed)usb("ERR lock before network probe\n");else mqttRequestProbe();}
      else if(!strcmp(line,"wifi ap"))networkRequestAction(NetworkAction::OpenAp);
      else if(!strcmp(line,"wifi forget")){if(controlSnapshot().armed)usb("ERR lock first\n");else networkRequestAction(NetworkAction::Forget);}
      else if(!strcmp(line,"ble info")){auto b=bleView();usb("BLE name=%s passkey=%06lu ready=%u connected=%u secure=%u\n",b.name,static_cast<unsigned long>(b.passkey),b.ready,b.connected,b.secure);}
      else if(!strcmp(line,"device info"))usb("DEVICE logical=%06lx hardware_suffix=%06lx; identity changes take effect after restart\n",static_cast<unsigned long>(deviceIdentityValue()),static_cast<unsigned long>(hardwareSuffix(ESP.getEfuseMac())));
      else if(!strncmp(line,"device id ",10)){if(controlSnapshot().armed||mqttView().enabled)usb("ERR lock both channels and disable MQTT before identity change\n");else usb(deviceIdentitySave(line+10,!strcmp(line+10,"new"))?"OK identity saved; restart before reconnecting MQTT\n":"ERR identity requires six hex digits or new\n");}
      else if(!strcmp(line,"memory"))memoryInfo();
      else if(!strcmp(line,"help"))usb("status | diag | memory | device info | device id new | logs | logs init | show 1 | show 2 | arm | disarm | on 1 | off 1 | off all | errors | errors clear | pwm info | manualresume on|off\nphoto 1 off|dark|bright|on | photo 1 dim 600 | dailyresume 1 on|off | recovery info | arm 1 | disarm 1 | wifi restart (lock)\ndaily 1 07:00:00-08:00:00,09:00:00-13:00:00\ncycle 1 3 0.5/0.3,2/1 (seconds; 0 repeats = infinite)\npwm 1 50 50 | pwm 1 20000 50 | pwm 1 off (runtime allowed)\nresume 1 advance 3600 60 | resume 1 pause 3600 60 | resume 0 advance 0 60\nnasresume on | nasresume off (lock)\npwmset 1 enable ON | pwmset 1 duty 50 | pwmset 1 frequency 1000\nwifi info | wifi radio | wifi sleep off | wifi sleep on (USB; lock; until reset) | wifi key (local secret display) | wifi ap | wifi forget | ble info | mqtt info | mqtt off | mqtt on | mqtt probe | test\n");
      else if(used&&!submit(line,Origin::Usb))usb("ERR command queue busy\n");used=0;overflow=false;
    }else if(!overflow){if(c<32||c>126||used>=sizeof(line)-1)overflow=true;else line[used++]=c;}
  }
}
void serviceButtons() {
#if INC_ENABLE_PHOTO
  static bool raw=true,stable=true;static uint32_t changed=0;bool r=digitalRead(INC_PAGE_BUTTON_PIN)!=LOW;if(r!=raw){raw=r;changed=millis();}if(stable!=r&&uint32_t(millis()-changed)>=40){stable=r;if(!r){page=(page+1)%6;lastPage=millis();}}
#else
  static bool raw[3]={true,true,true}, stable[3]={true,true,true};static uint32_t changed[3]={};
  for(uint8_t i=0;i<3;++i){bool r=digitalRead(BUTTON_PIN[i])!=LOW;if(r!=raw[i]){raw[i]=r;changed[i]=millis();}
    if(stable[i]!=r&&static_cast<uint32_t>(millis()-changed[i])>=40){stable[i]=r;if(!r){if(i==2){page=(page+1)%6;lastPage=millis();}else{char c[12];snprintf(c,sizeof(c),"%s %u",controlSnapshot().output[i]?"off":"on",i+1);submit(c,Origin::Button);}}}
  }
#endif
}
#if INC_ENABLE_OLED
// Compact labels keep both light blocks within the 128x64 display.
void oledDuration(char*text,size_t size,uint64_t ms){
 if(ms<60000){char seconds[16];decimal::seconds(seconds,sizeof(seconds),ms);snprintf(text,size,"%ss",seconds);}
 else if(ms<3600000)snprintf(text,size,"%llum",(ms+30000)/60000);
 else if(ms<86400000)snprintf(text,size,"%lluh",(ms+1800000)/3600000);
 else snprintf(text,size,"%llud",(ms+43200000)/86400000);
}
void oledRounds(char*text,size_t size,uint64_t n){if(n<10000)snprintf(text,size,"%llu",n);else if(n<1000000)snprintf(text,size,"%lluk",n/1000);else snprintf(text,size,"%lluM",n/1000000);}
__attribute__((noinline)) void oledLightOverview(const ControlSnapshot&s){
 const bool detail=(millis()/2000)%2;
 for(uint8_t ch=0;ch<2;++ch){const auto&c=s.config[ch];const auto&p=s.progress[ch];oled.setCursor(0,ch*32);oled.printf("L%u %s %s\n",ch+1,s.output[ch]?"ON":"OFF",modeName(c.mode));
  if(c.mode==program::Mode::Cycle){if(detail){char on[16],off[16];const uint8_t row=p.row<c.stepCount?p.row:0;oledDuration(on,sizeof(on),c.steps[row].onMs);oledDuration(off,sizeof(off),c.steps[row].offMs);oled.printf("ON:%s OFF:%s\n",on,off);}
   else{char done[16],total[16];oledRounds(done,sizeof(done),p.completed);if(c.repeats)oledRounds(total,sizeof(total),c.repeats);else snprintf(total,sizeof(total),"INF");oled.printf("S%u/%u R%s/%s\n",p.row+1,c.stepCount,done,total);}
  }else if(c.mode==program::Mode::Daily){const uint8_t row=(millis()/4000)%c.dailyCount;const auto&w=c.daily[row];if(detail)oled.printf("%02lu:%02lu:%02lu-%02lu:%02lu:%02lu\n",w.on/3600,(w.on/60)%60,w.on%60,w.off/3600,(w.off/60)%60,w.off%60);
   else oled.printf("D%u/%u ON %02lu:%02lu:%02lu\n",row+1,c.dailyCount,w.on/3600,(w.on/60)%60,w.on%60);
  }else oled.printf("Manual restore:%s\n",s.channelManualRecovery[ch]?"ON":"OFF");
  if(!detail&&c.mode==program::Mode::Cycle){char left[16];oledDuration(left,sizeof(left),p.remainingMs);oled.printf("Left:%s%s\n",left,p.finished?" DONE":"");}
  else if(!detail&&c.mode==program::Mode::Daily){const auto&w=c.daily[(millis()/4000)%c.dailyCount];oled.printf("OFF %02lu:%02lu:%02lu\n",w.off/3600,(w.off/60)%60,w.off%60);}
  else if(c.pwm)oled.printf("PWM:%uHz %u%%\n",c.frequency,c.duty);else oled.println("PWM:OFF");
 }
 oled.drawLine(0,26,127,26,SSD1306_WHITE);oled.setCursor(0,56);oled.printf("%s LIMIT:%u",s.armed?"ARMED":"LOCKED",s.limit);
}
#endif
void display() {
#if INC_ENABLE_OLED
  if(!oledReady||static_cast<uint32_t>(millis()-lastDisplay)<250)return;lastDisplay=millis();
  if(static_cast<uint32_t>(millis()-lastPage)>=6000){lastPage=millis();page=(page+1)%6;}
  const auto hAlert=healthView();static uint32_t displayedFaults=0;const uint32_t alert=(hAlert.active|hAlert.latched)&~FAULT_SHORT_PULSE;if(alert&&alert!=displayedFaults){page=4;lastPage=millis();}displayedFaults=alert;
  const auto s=controlSnapshot();oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(SSD1306_WHITE);oled.setCursor(0,0);
  if(page==0){oled.println(s.clockText[0]?s.clockText:"TIME UNSYNC");if(s.sensorValid){char t[20],h[20];decimal::sample(t,sizeof(t),s.temperature);decimal::sample(h,sizeof(h),s.humidity);oled.printf("T:%sC H:%s%%\n",t,h);}else oled.println(INC_ENABLE_DHT?"DHT11 invalid":"DHT module OFF");
    for(uint8_t i=0;i<2;++i)oled.printf("L%u %s %s\n",i+1,s.output[i]?"ON":"OFF",modeName(s.config[i].mode));oled.println(s.armed?"ARMED":"LOCKED");oled.println((s.channelDailyWaiting[0]||s.channelDailyWaiting[1])?"DAILY: WAIT CLOCK":s.resumeWaiting?"WAIT CLOCK TO RESUME":s.clockSource==ClockSource::Nas?"Clock: NAS":s.clockSource==ClockSource::Ntp?"Clock: NTP":"Clock: MANUAL/UNSYNC");
  }else if(page==1){oledLightOverview(s);
  }else if(page==2){const auto n=networkView();const auto b=bleView();oled.println(n.connected?"WiFi:CONNECTED":"WiFi:DISCONNECTED");oled.println(n.connected?n.ip:n.apIp);oled.println("SSID:");
    const char *name=n.connected?n.ssid:n.apName;
    unicode.setFont(u8g2_font_wqy12_t_gb2312);const int16_t width=unicode.getUTF8Width(name);
    const int16_t scroll=width>128?static_cast<int16_t>(((millis()-lastPage)/60)%(width+32)):0;
    unicode.setCursor(-scroll,35);unicode.print(name);if(width>128){unicode.setCursor(width+32-scroll,35);unicode.print(name);}
    oled.setCursor(0,40);oled.printf("BLE:%s\n",b.secure?"SECURE":b.connected?"PAIRING":b.advertisingPaused?"AP PAUSED":b.ready?"READY":"DISABLED");oled.printf("PIN:%06lu\n",static_cast<unsigned long>(b.passkey));oled.printf("MQTT:%s LIMIT:%u",mqttView().connected?"ON":"OFF",s.limit);
  }else if(page==4){auto h=healthView();oled.println("STATUS / ERRORS");oled.println(healthText(h.active));oled.printf("Active:%08lx\n",static_cast<unsigned long>(h.active));oled.printf("History:%08lx\n",static_cast<unsigned long>(h.latched));oled.printf("Rejected:%lu\n",static_cast<unsigned long>(h.commandErrors));oled.println(h.lastCommandError[0]?h.lastCommandError:"See web / errors");}
  else if(page==5){auto v=photoView();oled.println("LIGHT FEEDBACK");for(unsigned i=0;i<2;++i){auto c=v.channels[i];oled.printf("L%u raw:%u %s\n",i+1,c.raw,c.calibrated&&c.status!=9?(c.on?"LIGHT":"DARK"):"RAW ONLY");if(c.calibrated&&c.status!=9)oled.printf("Relative:%u.%u%% %s\n",c.level/10,c.level%10,c.fault?"MISMATCH":"");else oled.println("NAS analysis unavailable");}oled.println(photoAnalysisFresh()?"Analysis: NAS":"Raw ADC / NAS offline");}
  else{oled.printf("Recovery:%s\n",s.recovery.enabled?"ON":"OFF");oled.println(s.recovery.countOutage?"Outage: ADVANCE":"Outage: PAUSE");oled.printf("Restart >%lus\n",static_cast<unsigned long>(s.recovery.restartAfterSeconds));oled.printf("Save every %lus\n",static_cast<unsigned long>(s.recovery.checkpointSeconds));char gap[24];decimal::seconds(gap,sizeof(gap),s.recoveryGapMs);oled.printf("Est.gap:%ss\n",gap);oled.println(s.resumeWaiting?"Waiting for NTP":s.recovered?"Recovered":"No boot recovery");}
  oled.display();
#endif
}
__attribute__((noinline)) void serviceAuxiliary(){
  auto s=controlSnapshot();const auto a=photoView().automatic;photoSample(s.output[0],s.output[1],monoMs(),a.active?a.duty>0&&a.duty<100&&a.channel==1:s.config[0].pwm,a.active?a.frequency:s.config[0].frequency,a.active?a.duty:s.config[0].duty,a.active?a.duty>0&&a.duty<100&&a.channel==2:s.config[1].pwm,a.active?a.frequency:s.config[1].frequency,a.active?a.duty:s.config[1].duty);
  static uint32_t lastHealth=0; if(uint32_t(millis()-lastHealth)>=1000){lastHealth=millis();uint32_t faults=0;auto n=networkView();auto m=mqttView();auto p=photoView();auto d=eventLogView();if(INC_ENABLE_DHT&&s.uptimeMs>15000&&!s.sensorValid)faults|=FAULT_DHT;if(s.uptimeMs>30000&&!n.connected)faults|=FAULT_WIFI;if(m.enabled&&s.uptimeMs>45000&&!m.connected)faults|=FAULT_MQTT;faults|=loghealth::active(d.ready,d.pending,loghealth::recent(d.lastFailure[0]!=0,d.lastFailureAtMs,millis()));if(ESP.getFreeHeap()<8192||ESP.getMaxAllocHeap()<2048)faults|=FAULT_MEMORY;if(loghealth::changed(observedLogFailures,d.writeFailures)){healthLatch(FAULT_LOG_WRITE);observedLogFailures=d.writeFailures;}for(unsigned i=0;i<2;++i){if(p.channels[i].fault)faults|=i?FAULT_PHOTO2:FAULT_PHOTO1;if(s.config[i].pwm&&1000000ULL*s.config[i].duty/s.config[i].frequency/100<2)faults|=FAULT_SHORT_PULSE;}healthUpdate(faults);static uint32_t previousFaults=0;const uint32_t recorded=faults&(FAULT_DHT|FAULT_LOG|FAULT_LOG_WRITE|FAULT_PWM|FAULT_NVS|FAULT_CONTROLLER);static uint32_t lastHealthEvent=0;if(recorded!=previousFaults&&uint32_t(millis()-lastHealthEvent)>=10000){previousFaults=recorded;lastHealthEvent=millis();eventEmit("health",healthText(recorded),s,true);}}
}
}
bool controlEnqueue(const ControlCommand &c){return commands&&xQueueSend(commands,&c,0)==pdTRUE;}
void controlRead(ControlSnapshot &s){portENTER_CRITICAL(&stateMux);s=shared;portEXIT_CRITICAL(&stateMux);}
ControlSnapshot controlSnapshot(){ControlSnapshot s;controlRead(s);return s;}
void controlSensor(float t,float h,bool valid,bool ready){uint64_t sampled=monoMs();ClockSource clock;portENTER_CRITICAL(&stateMux);clock=shared.clockSource;portEXIT_CRITICAL(&stateMux);uint64_t epoch=clock!=ClockSource::Unsynced?epochMs():0;portENTER_CRITICAL(&stateMux);sensor={t,h,valid,ready,sampled,epoch,networkClock(clock)};portEXIT_CRITICAL(&stateMux);}
void applicationSetup(){
  loopTask=xTaskGetCurrentTaskHandle();
  for(uint8_t pin:LIGHT_PIN){pinMode(pin,OUTPUT);digitalWrite(pin,LOW);}Serial.begin(115200);Serial.setTxTimeoutMs(5);delay(1000);
  const bool identityOk=deviceIdentityStart();
  uint32_t memoryBefore=ESP.getFreeHeap();
  setenv("TZ",INC_TIMEZONE,1);tzset();logicPassed=program::selfTest()&&identityOk;loadSettings();if(!identityOk)healthLatch(FAULT_NVS);if(!storageReady){logicPassed=false;healthLatch(FAULT_NVS);}
  for(uint8_t i=0;i<2;++i)if(!configurePwm(i)){logicPassed=false;healthLatch(FAULT_PWM);}
  commands=xQueueCreate(4,sizeof(ControlCommand));publish();
  for(uint8_t pin:BUTTON_PIN)pinMode(pin,INPUT_PULLUP);
  memoryStage("control_setup",memoryBefore);memoryBefore=ESP.getFreeHeap();
  photoStart();memoryStage("photo",memoryBefore);memoryBefore=ESP.getFreeHeap();
#if INC_ENABLE_OLED
  Wire.begin(INC_I2C_SDA_PIN,INC_I2C_SCL_PIN,400000);Wire.setTimeOut(30);
  for(uint8_t a=0x3c;a<=0x3d;++a){Wire.beginTransmission(a);if(Wire.endTransmission()==0){oledReady=oled.begin(SSD1306_SWITCHCAPVCC,a,true,false);if(oledReady)break;}}
  if(oledReady){unicode.begin(oled);unicode.setFontMode(1);unicode.setFontDirection(0);unicode.setForegroundColor(SSD1306_WHITE);}
  #endif
  memoryStage("oled",memoryBefore);memoryBefore=ESP.getFreeHeap();
#if INC_ENABLE_DHT
  if(!dht.begin()){healthLatch(FAULT_DHT);usb("ERR DHT RMT initialization; sensor remains invalid\n");}
#endif
  memoryStage("dht",memoryBefore);memoryBefore=ESP.getFreeHeap();
  controlSensor(NAN,NAN,false,oledReady);lastSensor=millis();lastPage=millis();
  if(!eventLogStart())usb("ERR diagnostic log task unavailable\n");{auto boot=eventLogView();observedLogFailures=boot.writeFailures;if(boot.lastFailure[0])healthLatch(FAULT_LOG_WRITE);snprintf(currentBoot,sizeof(currentBoot),"%s",boot.bootId);}memoryStage("event_log",memoryBefore);memoryBefore=ESP.getFreeHeap();if(!mqttStart())usb("ERR MQTT initialization unavailable\n");memoryStage("mqtt_config",memoryBefore);memoryBefore=ESP.getFreeHeap();
  publish();eventEmit("boot","START",controllerEventSnapshot(),true);
  if(!commands||xTaskCreate(controllerTask,"inc-controller",8192,nullptr,2,nullptr)!=pdPASS){logicPassed=false;healthLatch(FAULT_CONTROLLER);usb("ERR controller unavailable; LOCKED\n");}
  memoryStage("controller_task",memoryBefore);memoryBefore=ESP.getFreeHeap();
  if(!networkStart())usb("ERR network unavailable\n");memoryStage("wifi_http",memoryBefore);memoryBefore=ESP.getFreeHeap();
  if(!bleStart())usb("ERR BLE unavailable\n");memoryStage("ble",memoryBefore);
  auto diagnostic=eventLogView();memoryInfo();
  usb("MODULES BLE=%u DHT=%u OLED=%u PHOTO=%u AO=%u,%u manual_resume=%u\n",INC_ENABLE_BLE,INC_ENABLE_DHT,INC_ENABLE_OLED,INC_ENABLE_PHOTO,INC_PHOTO1_PIN,INC_PHOTO2_PIN,manualRecoveryEnabled);
  usb("BOOT Connected-2.2.3 GPIO SDA%d SCL%d DHT%d L1=%d L2=%d limit=%u logic=%s reset=%s boot=%lu\nUse help, status, diag, mqtt info. First-use log storage: logs init.\n",INC_I2C_SDA_PIN,INC_I2C_SCL_PIN,INC_DHT_PIN,INC_LIGHT1_PIN,INC_LIGHT2_PIN,MAX_ACTIVE_CHANNELS,logicPassed?"PASS":"FAIL",diagnostic.resetReason,static_cast<unsigned long>(diagnostic.bootCount));
}
void applicationLoop(){
  serviceSerial();serviceButtons();
  #if INC_ENABLE_DHT
  if(static_cast<uint32_t>(millis()-lastSensor)>=5000){lastSensor=millis();float h=NAN,t=NAN;bool valid=dht.read(t,h);controlSensor(valid?t:NAN,valid?h:NAN,valid,oledReady);}
#endif
  serviceAuxiliary();
  mqttService();display();delay(1);
}

void controlNasTime(uint64_t echo,uint64_t epoch){const uint64_t offset=networktime::nasOffset(monoMs(),echo,epoch);if(!offset)return;portENTER_CRITICAL(&stateMux);nasClockOffset=offset;portEXIT_CRITICAL(&stateMux);}
