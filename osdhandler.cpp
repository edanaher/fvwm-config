#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <xosd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <time.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <gnutls/gnutls.h>

//#define GEMEDET
//#define LONEP
#define CHILEH

/* Set compiled-in modules */
#ifdef LONEP
   #define MUSICPLAYER
   #define PULSEAUDIO
   #define VOLUME
   #define MAIL
   #define FAN
   #define WIRELESS
   #define HDSPIN
   #define BATTERY
   #define MAILIDLER
#endif
#ifdef CHILEH
   #define MUSICPLAYER
//   #define JACK_AUDIO
   #define VOLUME
   #define MAIL
   #define TEMPERATURE
   #define MAILIDLER
#endif


#ifdef JACK_AUDIO
#include <jack/jack.h>
#endif /* JACK_AUDIO */

#ifdef PULSEAUDIO
#include <pulse/pulseaudio.h>
#endif /* PULSEAUDIO */

#ifdef MUSICPLAYER
#include <taglib/fileref.h>
#include <taglib/taglib.h>
#include <taglib/tag.h>
#include <xine.h>
#include <xine/audio_out.h>
#endif /* MUSICPLAYER */



#define _(x) ((char *)(x))
#define max(a,b)  (((a) > (b)) ? (a) : (b))
#define max4(a,b,c,d)  (max(max((a),(b)), max((c),(d))))
#define min(a,b)  (((a) < (b)) ? (a) : (b))
#define RUN_EVERY(n,v) \
      static long long lastrun = 0; \
      if(now - lastrun < n) { \
         strcpy(out, v); \
         return; \
      } \
      lastrun = now;
#define OPEN_FILE(f,v) FILE *FIN = fopen(f, "r"); \
      if(!FIN) { v[0] = out[0] = 0; return; }
#define OPEN_FILE2(f,v) FIN = fopen(f, "r"); \
      if(!FIN) { v[0] = out[0] = 0; return; }


inline int ugetnow() {
   struct timeval now;
   gettimeofday(&now, NULL);
   return now.tv_sec * 1000000 + now.tv_usec;
}
#if 1
#define TIME(counter,command) command
#else 
#define TIME(counter,command) do { \
   time_counter_##counter -= ugetnow(); \
   command; \
   time_counter_##counter += ugetnow(); \
   } while(0)
#define DEFCOUNTER(c) int time_counter_##c
DEFCOUNTER(time);
DEFCOUNTER(battery);
DEFCOUNTER(cpu);
DEFCOUNTER(mem);
DEFCOUNTER(fan);
DEFCOUNTER(wireless);
DEFCOUNTER(netuse);
DEFCOUNTER(hd);
DEFCOUNTER(mail);
DEFCOUNTER(music);
DEFCOUNTER(tmp1);
DEFCOUNTER(tmp2);
typedef struct {
   int val;
   char* name;
} NamedCounter;

int counter_cmp(const void *a, const void *b) {
   return *(int *)b - *(int *)a;
}

#define NAMECOUNTER(c) { time_counter_##c, (char *)#c}
void printCounters() {
   NamedCounter cs[] = {
      NAMECOUNTER(time),
      NAMECOUNTER(battery),
      NAMECOUNTER(cpu),
      NAMECOUNTER(mem),
      NAMECOUNTER(fan),
      NAMECOUNTER(wireless),
      NAMECOUNTER(netuse),
      NAMECOUNTER(hd),
      NAMECOUNTER(mail),
      NAMECOUNTER(music),
      NAMECOUNTER(tmp1),
      NAMECOUNTER(tmp2)
   };
   int i, total = 0, len = sizeof(cs) / sizeof(cs[0]);
   for(i = 0; i < len; i++)
      total += cs[i].val;
   printf("total: %d  ", total);
   qsort(cs, len, sizeof(cs[0]), counter_cmp);
   for(i = 0; i < len; i++) {
      if(cs[i].val) printf("%s: %d  ", cs[i].name, cs[i].val);
      total += cs[i].val;
   }
   printf("\n");
}
#endif

class Clock;

long long now;
int running = 1;
const char *prompts = ":=-^";

class NiceOsd;
NiceOsd *osds = NULL;

const char numbers[] = "0123456789abcdefghijk";
#ifdef MAIL
int inotify_fd;
#endif /* MAIL */

char *stracpy(char *str) {
   if(!str) return NULL;
   int len = strlen(str);
   char *ret = (char *)malloc(len + 1);
   memcpy(ret, str, len + 1);
   return ret;
}

char *memacpy(char *str, int len) {
   char *ret = (char *)malloc(len);
   memcpy(ret, str, len);
   return ret;
}

class NiceOsd {
   int num_lines_;
   int lines_used_;
   xosd *osd_;
   int timeout_;
   long long last_write_;


   public:
      NiceOsd *next_osd_;

   NiceOsd(xosd_align align, xosd_pos pos, int num_lines) {
      next_osd_ = osds;
      osds = this;
      osd_ = xosd_create(num_lines);
      num_lines_ = num_lines;
      lines_used_ = 0;
      timeout_ = 1;
      //xosd_set_width(osd_, 1920);
      xosd_set_font(osd_, "10x20");
      xosd_set_colour(osd_, "#C0C0C0");
      xosd_set_pos(osd_, pos);
      xosd_set_vertical_offset(osd_, 2);
//      xosd_set_vertical_offset(osd_, 20);
      xosd_set_align(osd_, align);
   }

   void SetScreenWidth(int width) {
      //xosd_set_width(osd_, width);
   }

   void SetScreenHeight(int height) {
      //xosd_set_height(osd_, height);
   }

   void Clear() {
      xosd_scroll(osd_, num_lines_);
      lines_used_ = 0;
   }

   void WriteLine(char *str, int base_line) {
      int i,  j;
      //printf("line is %s\n", str);
      for(i = 0; str[i]; i++)
         if(str[i] < 0 && str[i] != -2 && str[i] != -22 && str[i] != -23 && str[i] != -24 && str[i] != -55 &&
            str[i] != -10 && str[i] != -4 && str[i] != -31 && str[i] != -7 && str[i] != -17 && str[i] != -32 &&
            str[i] != -13)
            printf("Bad char in line: %d %c\n", str[i], str[i]);
      for(i = 0; str[i]; i++) {
         if(str[i] == -22) str[i] = 'e';
         if(str[i] == -23) str[i] = 'e';
         if(str[i] == -24) str[i] = 'e';
         if(str[i] == -55) str[i] = 'E';
         if(str[i] == -32) str[i] = 'a';
         if(str[i] == -31) str[i] = 'a';
         if(str[i] == -17) str[i] = 'i';
         if(str[i] == -13) str[i] = 'o';
         if(str[i] == -10) str[i] = 'o';
         if(str[i] == -7) str[i] = 'u';
         if(str[i] == -4) str[i] = 'u';
         if(str[i] == -2) str[i] = 't';
      }
      for(i = base_line; i < num_lines_; i++) {
         for(j = 0; str[j] && str[j] != '\n'; j++);
         if(!str[j])
            break;
         str[j] = 0;
         xosd_display(osd_, i, XOSD_string, str);
         str += j + 1;
      }
      lines_used_ = i;
      if(i < num_lines_) {
         xosd_display(osd_, i++, XOSD_string, str);
         i--;
      }
      last_write_ = now;
   }

   void Append(char *str) {
      int i, lines = 0;
      if(timeout_ >= 0 && now - last_write_ > 1000 * timeout_)
         Clear();

      for(i = 0; str[i]; i++)
         if(str[i] == '\n')
            lines++;
      if(lines_used_ + lines > num_lines_) {
         xosd_scroll(osd_, lines_used_ + lines - num_lines_);
         lines_used_ = max(num_lines_ - lines, 0);
      }
      WriteLine(str, lines_used_);
   }


   void Write(char *str) {
      if(num_lines_ > 1)
         Clear();
      WriteLine(str, 0);
   }

   void Show() {
      xosd_show(osd_);
   }

   void Hide() {
      xosd_hide(osd_);
   }

   void SetTimeOut(int n) {
      xosd_set_timeout(osd_, n);
      timeout_ = n;
   }

   void SetFont(char *f, char *c, char *s, int os) {
      xosd_set_font(osd_, f);
      xosd_set_colour(osd_, c);
      xosd_set_shadow_colour(osd_, s);
      xosd_set_shadow_offset(osd_, os);
   }
};
NiceOsd UL_osd(XOSD_left, XOSD_top, 10);
NiceOsd UR_osd(XOSD_right, XOSD_top, 10);

#ifdef MAILIDLER
#define IMAP_SSL_PORT 993
#define GAHLPO_IP "216.218.223.91"
class MailIdler {
   gnutls_session_t session_;
   int comnum_;

   public:

   enum STATUS { DISCONNECTED, CONNECTING, IDLING, ERROR, NEW};
   int fd;
   long long timeout_;
   STATUS status_;

   MailIdler()  {
      fd = -1;
      comnum_ = 3;
      status_ = DISCONNECTED;
      InitTLS();
      Connect();
      timeout_ = now + 5 * 60 * 1000;
   }

   int InitTLS() {
      int err;
      struct sockaddr_in sa;
      gnutls_anon_client_credentials_t anoncred;

      if(fd != -1)
         close(fd);

      gnutls_global_init();
      gnutls_anon_allocate_client_credentials(&anoncred);
      gnutls_init(&session_, GNUTLS_CLIENT);
      gnutls_priority_set_direct(session_, "PERFORMANCE:+ANON-DH:!ARCFOUR-128", NULL);
      gnutls_credentials_set(session_, GNUTLS_CRD_ANON, anoncred);

      fd = socket(AF_INET, SOCK_STREAM, 0);
      memset(&sa, 0, sizeof(sa));
      sa.sin_family = AF_INET;
      sa.sin_port = htons(IMAP_SSL_PORT);
      inet_pton(AF_INET, GAHLPO_IP, &sa.sin_addr);
      err = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
      if(err < 0) {
         return -1;
      }

      gnutls_transport_set_ptr(session_, (gnutls_transport_ptr_t) fd);
      err = gnutls_handshake(session_);
      if(err < 0) {
         gnutls_perror(err);
         return -1;
      }
      return 0;
   }

   char *SendCommand(char *send, char *reply) {
      char buf[1024];
      int len;
      if(status_ == ERROR)
         return NULL;
      if(send) {
         len = gnutls_record_send(session_, send, strlen(send));
         if(len < 0) {
            printf("MailIdler: Send error: %d, %m\n", len);
            status_ = ERROR;
            return NULL;
         }
      }

      len = gnutls_record_recv(session_, buf, 1024);
      if(len <= 0) {
         printf("MailIdler: Receive error: %d, %m\n", len);
         status_ = ERROR;
         return NULL;
      }
      while(len > 0 && buf[len - 2] == '\n' || buf[len - 2] == '\r')
         buf[len-- - 2] = 0;
      if(reply) {
         if(strcmp(buf, reply)) {
            printf("MailIdler: Received \"%s\", expecting \"%s\"\n", buf, reply);
            return NULL;
         } else
            return reply;
      } else {
         return stracpy(buf);
      }
   }

   int Connect() {
      char buf[1024];
      int len;
      status_ = CONNECTING;
      if(!SendCommand(NULL, _("* OK Dovecot ready.")))
         return -1;

      int passfd = open("/home/edanaher/.fvwm/.imappasswd", O_RDONLY);
      if(passfd < 0) {
         printf("MailIdler: Unable to open IMAP password file\n");
         return -1;
      }
      strcpy(buf, "A001 LOGIN edanaher ");
      read(passfd, buf + strlen(buf), 20);
      close(passfd);

      if(!SendCommand(buf, _("A001 OK Logged in.")))
         return -1;

      char *msg;
      if(!(msg = SendCommand(_("A002 SELECT INBOX\n"), NULL)))
         return -1;
      printf("MailIdler received message: -----------\n%s\n------------\n", msg);

      if(!SendCommand(_("A003 IDLE\n"), _("+ idling")))
         return -1;
      status_ = IDLING;
      comnum_ = 3;
      return 0;
   }

   int ReIdle() {
      char sbuf[1024], rbuf[1024];

      sprintf(rbuf, "A%03d OK Idle completed.", comnum_);
      printf("MailIdler: Receiving idle A%03d completed\n", comnum_);
      if(!SendCommand(_("DONE\n"), rbuf))
         return -1;

      sprintf(sbuf, "A%03d IDLE\n", ++comnum_);
      printf("MailIdler: Sending idle A%03d\n", comnum_);
      if(!SendCommand(sbuf, _("+ idling")))
         return -1;

      printf("MailIdler: Reidled\n");

      return 0;
   }

   int Handle(int receive) {
      static int count = 0;

      if(status_ == ERROR || timeout_ < now) {
         printf("MailIdler: Error state %d == %d or %lld < %lld; reconnecting\n", status_, ERROR, timeout_, now);
         timeout_ = now + 60 * 1000;
         if(InitTLS() < 0)
            return -1;
         if(Connect() < 0)
            return -1;
      }

      timeout_ = now + 5 * 60 * 1000;
      if(!receive)
         return 0;

      char *buf = SendCommand(NULL, NULL);
      if(buf == NULL)
         return -1;
      printf("MailIdler needs to do something: %s\n", buf);

      if(strstr(buf, "EXISTS")) {
         printf("MailIdler: New message!\n");
         status_ = NEW;
      }
      if(strstr(buf, "FETCH")) {
         printf("MailIdler: Fetched messages!\n");
         status_ = IDLING;
      }
      if(buf)
         free(buf);

      if(++count == 14) {
         ReIdle();
         count = 0;
      }

      return 0;
   }
} mail_idler;
#undef IMAP_SSL_PORT
#undef GAHLPO_IP
#endif /* MAILIDLER */

#define fscanf1(f, s, ...) if(fscanf(f, s, ##__VA_ARGS__) != 1) printf("Failed to find string %s", s)

class Clock {
   NiceOsd osd_;
#ifdef VOLUME
   char volume_[10];
#endif /* VOLUME */
   int ac_;
   int full_;

   static const int num_inotifies = 4;

   char *maildirs[num_inotifies];

   int hd_status_fds[2][2];
   public:

   Clock() :
      osd_(XOSD_right, XOSD_bottom, 1),
      ac_(-1),
      full_(0) {
      maildirs[0] = _("/home/edanaher/.maildir/new/");
      //maildirs[1] = _("/home/edanaher/.maildir/gmail/new/");
      maildirs[1] = _("/home/edanaher/.maildir/sf2g/new/");
      maildirs[2] = _("/home/edanaher/.maildir/crm-unsure/new/");
      maildirs[3] = _("/home/edanaher/.maildir/crm-spam/new/");

#ifdef MAIL
      if((inotify_fd = inotify_init()) < 0)
         printf("Inotify initialization failed\n");

      int i;
      for(i = 0; i < num_inotifies; i++)
         if(inotify_add_watch(inotify_fd, maildirs[i], IN_CREATE
                                                  | IN_DELETE
                                                  | IN_MOVE) < 0)
            printf("Inotify add watch failed\n");
#endif /* MAIL */

      pipe(hd_status_fds[0]);
      pipe(hd_status_fds[1]);
      if(!fork()) {
         close(hd_status_fds[0][1]);
         close(hd_status_fds[1][0]);
         dup2(hd_status_fds[0][0], STDIN_FILENO);
         dup2(hd_status_fds[1][1], STDOUT_FILENO);
         execl("/home/edanaher/.fvwm/hd/hd_status", "/home/edanaher/.fvwm/hd/hd_status", NULL);
      }
      close(hd_status_fds[1][1]);
      close(hd_status_fds[0][0]);
   }

   void GetTime(char *out) {
      static char date[100];
      static const char *weekdays = "UMTWRFSU";
      time_t secs = now/1000;
      RUN_EVERY(1000, date);

      strftime(date, 100, "%u %m/%d %H:%M:%S", localtime(&secs));
      if(date[5] == '0') memmove(date + 5, date + 6, 11);
      if(date[2] == '0') memmove(date + 2, date + 3, 15);
      date[0] = weekdays[date[0] - '0'];
      strcpy(out, date);
   }

   void GetBatteries(char *out) {
      static char rate[50], left[50], in[100], bat[50], charging[50], c;
      static int inUse;
      int current_now, charge_now;
      RUN_EVERY(30000, bat);
      FILE *FIN;

      int lowBat = 3, leftN = 0;

      bat[0] = 0;
      inUse = 0;
      for(int b = 0; b < 1; b++) {
         if(ac_ && full_) continue;
         sprintf(charging, "/sys/class/power_supply/BAT%d/status", b);
         FIN = fopen(charging, "r");
         if(!FIN) continue;
         fscanf(FIN, "%s", charging);
         fclose(FIN);
         if(!strcmp(charging, "Full") && ac_) {
            full_ = 1;
            continue;
         }

         inUse = 1;
         c = ac_ ? '+' : '-';

         sprintf(charging, "/sys/class/power_supply/BAT%d/current_now", b);
         FIN = fopen(charging, "r");
         if(!FIN) continue;
         fscanf(FIN, "%d", &current_now);
         fclose(FIN);

         sprintf(charging, "/sys/class/power_supply/BAT%d/charge_now", b);
         FIN = fopen(charging, "r");
         if(!FIN) continue;
         fscanf(FIN, "%d", &charge_now);
         fclose(FIN);

         sprintf(bat + strlen(bat), "%d%c%d", charge_now/100000, c, current_now/10000);
         /*if(leftN > 2000 || c == '+' || c == '*')
            lowBat &= 0;
         if(leftN > 1000 || c == '+' || c == '*')
            lowBat &= 1;*/
      }
      if(!inUse)
         bat[0] = 0;
      /*if(lowBat & 2)
         sprintf(bat + strlen(bat), "---VERYLOWBAT---");
      else if(lowBat & 1)
         sprintf(bat + strlen(bat), "---LOWBAT---");*/
      strcpy(out, bat);
   }

#ifdef CHILEH
   void GetCpu(char *out) {
      static char usage[100];
      static int lastworking[4], lasttotal[4];
      int working, total;
      int user, nice, sys, idle, wait, irq, softirq, vmm;
      char line[100], junk[5];
      int cpuusage;

      static const char *separators = ".,:;|";
      int freq, numfast = 0;
      char separator;

      RUN_EVERY(1000, usage);

      OPEN_FILE("/proc/stat", usage);
      fgets(line, 100, FIN);
      usage[0] = '.';
      usage[1] = 0;
      char letters[] = ".abcdefghi";
      for(int p = 0; p < 4; p++) {
         fgets(line, 100, FIN);
         sscanf(line, "%s %d %d %d %d %d %d %d", junk, &user, &nice,
               &sys, &idle, &wait, &irq, &softirq, &vmm);

         working = user + nice + sys;
         total = working + idle + wait + irq + softirq;
         cpuusage = (int)(100.0*
                  (working - lastworking[p]) /
                  (total - lasttotal[p]));

         if(cpuusage < 10)
            sprintf(usage + strlen(usage), "%c", letters[max(cpuusage, 0)]);
         else if(cpuusage < 100)
            sprintf(usage + strlen(usage), "%d", cpuusage / 10);
         else
            sprintf(usage + strlen(usage), "#");
         lastworking[p] = working;
         lasttotal[p] = total;
      }
      /*usage[2] = separator[0];
      usage[3] = separator[1];*/
      fclose(FIN);

      char filename[100];

      for(int p = 0; p < 4; p++) {
         sprintf(filename, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", p);
         OPEN_FILE2(filename, usage);
         fscanf(FIN, "%d", &freq);
         fclose(FIN);
         if(freq > 1600000)
            numfast++;
      }
      usage[0] = separators[numfast];
            
      strcpy(out, usage);
   }
#else /* LONEP */
   void GetCpu(char *out) {
      static char usage[100];
      static int lastworking[2], lasttotal[2];
      int working, total;
      int user, nice, sys, idle, wait, irq, softirq, vmm;
      char line[100], junk[5];
      int cpuusage;
      RUN_EVERY(1000, usage);
      OPEN_FILE("/proc/stat", usage);
      fgets(line, 100, FIN);
      usage[0] = 0;
      for(int p = 0; p < 2; p++) {
         fgets(line, 100, FIN);
         sscanf(line, "%s %d %d %d %d %d %d %d", junk, &user, &nice,
               &sys, &idle, &wait, &irq, &softirq, &vmm);

         working = user + nice + sys;
         total = working + idle + wait + irq + softirq;
         cpuusage = (int)(100.0*
                  (working - lastworking[p]) /
                  (total - lasttotal[p]));

         if(cpuusage < 100)
            sprintf(usage + strlen(usage), !p ? "%2d." : "%-2d", max(cpuusage, 0));
         else
            sprintf(usage + strlen(usage), !p ? "##." : "##");
         lastworking[p] = working;
         lasttotal[p] = total;
      }
      fclose(FIN);

      strcpy(out, usage);
   }
#endif /* LONEP */

   void GetMem(char *out) {
      static char memusage[100];
      char junk[100];
      int memtotal, memfree, buffers, cached, swaptotal, swapfree;
      int memprogs, memfiles, swapused, i;
      RUN_EVERY(1000, memusage);
      OPEN_FILE("/proc/meminfo", memusage);

      fscanf1(FIN, "MemTotal: %d kB\n", &memtotal);
      fscanf1(FIN, "MemFree: %d kB\n", &memfree);
      fgets(junk, 100, FIN);
      fscanf1(FIN, "Buffers: %d kB\n", &buffers);
      fscanf1(FIN, "Cached: %d kB\n", &cached);
      for(i = 0; i < 9; i++)
         fgets(junk, 100, FIN);
      fscanf1(FIN, "SwapTotal: %d kB\n", &swaptotal);
      fscanf1(FIN, "SwapFree: %d kB\n", &swapfree);
      fclose(FIN);

      memfiles = cached + buffers;
      memprogs = memtotal - memfree - memfiles;
      swapused = swaptotal - swapfree;
      if(memtotal == 0)
         printf("memtotal is 0!\n");
      /*if(swaptotal == 0)
         swaptotal = 1;*/

      sprintf(memusage, "%c%c%c", numbers[20*memprogs / memtotal],
                                  numbers[20*memfiles / memtotal],
                                  swaptotal ? numbers[20 * swapused / swaptotal] : '_');
      strcpy(out, memusage);
   }

   void GetFan(char *out) {
      static char fan[100];
      const char status[] = ",*#";
      int temp, f1, f2;
      RUN_EVERY(8000, fan);
      OPEN_FILE("/proc/i8k", fan);
      fscanf(FIN, "1.0 A11 C49DDG1 %d %d %d", &temp, &f1, &f2);
      fclose(FIN);
      sprintf(fan, "%d%c", temp, status[f2]);
      strcpy(out, fan);
   }

   void GetTemperature(char *out) {
      static char temperature[100];
      int temp, maxtemp = 0, p;
      FILE *FIN;
      char filename[100];
      RUN_EVERY(800, temperature);
      for(p = 1; p <= 5; p++) {
         sprintf(filename, "/sys/class/hwmon/hwmon0/device/temp%d_input", p);
         OPEN_FILE2(filename, temperature);
         fscanf(FIN, "%d", &temp);
         temp /= 1000;
         fclose(FIN);
         if(temp > maxtemp)
            maxtemp = temp;
      }
      sprintf(temperature, "%d", maxtemp);
      strcpy(out, temperature);
   }

   void GetWireless(char *out) {
      static char wireless[100], in[100];
      char junk;
      int w1, w2, w3;
      RUN_EVERY(1000, wireless);
      OPEN_FILE("/proc/net/wireless", wireless);
      fgets(in, 100, FIN);
      fgets(in, 100, FIN);
      if(fscanf(FIN, " %s %s %d%c %d%c %d%c", in, in, &w1, &junk, &w2, &junk, &w3, &junk) <= 4) {
         out[0] = wireless[0] = 0;
         fclose(FIN);
         return;
      }
      fclose(FIN);
      sprintf(wireless, "%c%c%c", numbers[20*w1/100],
                                  numbers[-20*w2/100],
                                  numbers[-20*w3/256]);
      //sprintf(wireless, "%2d %2d %2d", w1, -w2, -w3);
      if(!w1 && !w2 && !w3) 
         wireless[0] = 0;
      strcpy(out, wireless);
   }

   void GetHDSpin(char *out) {
      static char hd[2] = " ", in[201];
      const char *states = "oxOX?";
      int junk;
      int i, len;
      long long blocks[2];
      int logblocks[2];
      static int old_blocks[2] = {0,0};
      static int state = 0;
      RUN_EVERY(3000, hd);

      OPEN_FILE("/sys/block/sda/stat", hd);
      fscanf(FIN, "%d %d %lld %d %d %d %lld", &junk, &junk, &blocks[0], &junk, &junk, &junk, &blocks[1]);
      fclose(FIN);

      for(i = 0; i < 2; i++) {
         logblocks[i] = (int)(log((blocks[i] - old_blocks[i]) / 3) / log(2)) - 1;
         if(logblocks[i] > 20 || logblocks[i] < 0)
            logblocks[i] = 0;
      }
      old_blocks[0] = blocks[0];
      old_blocks[1] = blocks[1];

      len = write(hd_status_fds[0][1], "/dev/sdb\n", strlen("/dev/sdb\n"));
      if(len != strlen("/dev/sdb\n")) { hd[0] = out[0] = 0; return; }
      if(read(hd_status_fds[1][0], in, 2) != 2) { hd[0] = out[0] = 0; return; }
      in[1] = 0;
      if(!strcmp(in, "1"))
         state = 2;
      else if(!strcmp(in, "0"))
         state = 0;
      else
         state = 4;

      hd[0] = numbers[max(0, logblocks[0])];
      hd[1] = states[state];
      hd[2] = numbers[max(0, logblocks[1])];
      hd[3] = 0;
      strcpy(out, hd);
   }

   void GetNetuse(char *out) {
      static char netuse[100], in[200], iface[100], ifacen;
      const char ifaces[4][6] = {"lo", "eth0", "wlan0"};
      static long long last[4][2] = {{0,0},{0,0},{0,0},{0,0}};
      long long cur[4][2] = {{0,0},{0,0},{0,0},{0,0}}, diffs[4][2], bytes[2]; //rx, tx
      int junk;
      char ifacesym = 0;
      int i, tx, logusage[2];

      RUN_EVERY(1000, netuse);
      OPEN_FILE("/proc/net/dev", netuse);
      fgets(in, 200, FIN);
      fgets(in, 200, FIN);
      while(1) {
         if(!fgets(in, 200, FIN))
            break;
         *(strchr(in, ':')) = ' ';
         sscanf(in, " %s %lld %d %d %d %d %d %d %d %lld", iface, &bytes[0], &junk, &junk, &junk, &junk, &junk, &junk, &junk, &bytes[1]);
         for(ifacen = 0; strcmp(iface, ifaces[ifacen]) && ifacen < 4; ifacen++);
         cur[ifacen][0] = bytes[0];
         cur[ifacen][1] = bytes[1];
      }
      fclose(FIN);

      for(i = 1; i <= 2; i++)
         for(tx = 0; tx < 2; tx++)
         diffs[i][tx] = cur[i][tx] - last[i][tx];

      for(tx = 0; tx < 2; tx++) {
         if(cur[3][tx] || (diffs[1][tx] && diffs[2][tx])) ifacesym = '!';
         if(diffs[1][tx])                                 ifacesym = '"';
         if(diffs[2][tx])                                 ifacesym = '\'';
         if(!diffs[1][tx] && !diffs[2][tx] && !ifacesym)  ifacesym = '.';
         for(i = 0; i < 4; i++)
            last[i][tx] = cur[i][tx];

         logusage[tx] = (int)(log(diffs[1][tx] + diffs[2][tx]) / log(2)) - 10;
         if(!diffs[1][tx] && !diffs[2][tx])
            logusage[tx] = 0;
         if(logusage[tx] > 20)
            logusage[tx] = 0;
      }

      sprintf(netuse, "%c%c%c", ifacesym, numbers[max(0, logusage[0])], numbers[max(0, logusage[1])]); 
      strcpy(out, netuse);
   }

   void GetMail(char *out) {
      static char mail_str[100];
      char mail_idler_c;
      char buf[1024];
      int update = 0, i, messages;
      
      RUN_EVERY(1000, mail_str);

      timeval timeout = {0, 0};
      fd_set read_fds;
      FD_ZERO(&read_fds);
#ifdef MAIL
      FD_SET(inotify_fd, &read_fds);
      while(select(inotify_fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
         read(inotify_fd, buf, 1024);
         update = 1;
      }
#endif /* MAIL */
      if(update) {
         for(i = 0; i < num_inotifies; i++) {
            DIR * dent = opendir(maildirs[i]);
            for(mail_str[i] = -2; readdir(dent); mail_str[i] < 127 && mail_str[i]++);
            closedir(dent);
         }
         for(i = 0; i < num_inotifies; i++) {
            if(mail_str[i])
               update = 2;
            if(mail_str[i] == 0)
               mail_str[i] = '-';
            else if(mail_str[i] < 10)
               mail_str[i] += '0';
            else if(mail_str[i] < 36)
               mail_str[i] += 'A' - 10;
            else
               mail_str[i] = '@';
         }
         /*for(i = num_inotifies; i > 0; i--)
            mail_str[i] = mail_str[i-1];
         mail_str[0] = ' ';*/
         mail_str[num_inotifies] = ' ';
         mail_str[num_inotifies+1] = 0;
         if(update == 1)
            mail_str[0] = 0;
      }

#ifdef MAILIDLER
      if(mail_idler.status_ == mail_idler.ERROR)
         mail_idler_c = '!';
      else if(mail_idler.status_ == mail_idler.NEW)
         mail_idler_c = '*';
      else
         mail_idler_c = 0;

      //printf("mail_idler_c: '%c', mail_str: %s\n", mail_idler_c, mail_str);
      if(mail_idler_c && mail_str[0] != 0) {
         mail_str[num_inotifies] = mail_idler_c;
         mail_str[num_inotifies+1] = ' ';
         mail_str[num_inotifies+2] = 0;
      } else if(mail_idler_c && mail_str[0] == 0) {
         sprintf(mail_str, "----%c ", mail_idler_c);
      } else if(!mail_idler_c && !strncmp(mail_str, _("----"), 4)) {
         mail_str[0] = 0;
      } else {
         mail_str[num_inotifies] = ' ';
         mail_str[num_inotifies+1] = 0;
      }
#endif /* MAILIDLER */

      strcpy(out, mail_str);
   }
    
#ifdef VOLUME
   void SetVolume(int v, char on) {
      char tmp[10];
      if(v < 100)
         sprintf(tmp, "%c%d", (on == 'f' ? '-' : ' '), v);
      else
         sprintf(tmp, "%c##", (on == 'f' ? '-' : ' '));
      sprintf(volume_, "%3s", tmp);
   }
#endif /* VOLUME */

   void Update() {
      char output[1000];
      char date[100];
      char temp[10];
      char battery[100];
      char cpu[10];
      char mem[10];
      char speed[10];
      char fan[10];
      char temperature[10];
      char hd[10];
      char wireless[100];
      char netuse[100];
      char mail[10];
      FILE *FIN;

      TIME(time, GetTime(date));
#ifdef BATTERY
      TIME(battery, GetBatteries(battery));
#endif /* BATTERY */
      TIME(cpu, GetCpu(cpu));
      TIME(mem, GetMem(mem));
#ifdef FAN
      TIME(fan, GetFan(fan));
#endif /* FAN */
#ifdef TEMPERATURE
      TIME(temperature, GetTemperature(temperature));
#endif /* FAN */
#ifdef WIRELESS
      TIME(wireless, GetWireless(wireless));
#endif /* WIRELESS */
      TIME(netuse, GetNetuse(netuse));
#ifdef HDSPIN
      TIME(hd, GetHDSpin(hd));
#endif /* HDSPIN */
#ifdef MAIL
      TIME(mail, GetMail(mail));
#endif /* MAIL */

#ifdef LONEP
      sprintf(output, "%s %s %s%s %s %s %s %s %s%s", battery, volume_, wireless, netuse, fan, hd, mem, cpu, mail, date);
#else
      sprintf(output, "%s %s %s %s%s %s%s", volume_, netuse,  mem, temperature, cpu, mail, date);
#endif /* LONEP */

      osd_.Write(output);
   }

   void Handle(char *com) {
      if(!strcmp(com, "ac"))
         ac_ = 1;
      else if(!strcmp(com, "bat")) {
         ac_ = 0;
         full_ = 0;
      }
   }

   void Hide() {
      osd_.Hide();
   }
} osd_clock;

void executer_handler_wrapper(char *c);
#ifdef PULSEAUDIO
void pulse_handler_wrapper();
#endif /* PULSEAUDIO */
#ifdef JACK_AUDIO
void jack_handler_wrapper();
#endif /* JACK_AUDIO */
void mouse_handler_wrapper(char *c);

class Keyboard_Grabber {
   Display *disp;
   Window root;

   XEvent evt;
   public:
   int x, y;
   enum user {EXECUTER, PULSE, MOUSE, JACK, NONE};

   private:
   user who_;

   public:
   void (*cb)(char *c);
   int fd;

   int shift_pressed;
   int control_pressed;
   int alt_pressed;

   Keyboard_Grabber() {
      who_ = NONE;
      cb = NULL;
      shift_pressed = 0;
      control_pressed = 0;
      alt_pressed = 0;
      disp = XOpenDisplay(NULL);
      root = RootWindow(disp, DefaultScreen(disp));
      fd = ConnectionNumber(disp);
      //printf("Grab: %d %d %d %d\n", XGrabKey(disp, KeysymToKeycode(disp, XK_F14), 0, root, False, GrabModeAsync, GrabModeAsync), BadAccess, BadValue, AlreadyGrabbed);
   }

   void grab(user who) {
      who_ = who;

      int delay, ret = AlreadyGrabbed;
      // X hackery.  For some reason, the keyboard is grabbed for a bit;
      // presumably FVWM's work.  So try for a bit and see if it's released.
      for(delay = 1000; ret == AlreadyGrabbed && delay < 1000000; delay *= 2) {
         usleep(delay);
         ret = XGrabKeyboard(disp, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
      }
      XAllowEvents(disp, AsyncKeyboard, CurrentTime);
      XFlush(disp);
   }

   void release() {
      who_ = NONE;
      XUngrabKeyboard(disp, CurrentTime);
      XFlush(disp);
   }

   int key_available() {
      return XCheckWindowEvent(disp, root, KeyPressMask | KeyReleaseMask, &evt);
   }

   int get_key() {
      int keyPressed = XKeycodeToKeysym(disp, evt.xkey.keycode, shift_pressed);
      x = evt.xkey.x;
      y = evt.xkey.y;
      switch(keyPressed) {
         case XK_Shift_L:
         case XK_Shift_R:
            shift_pressed = evt.type == KeyPress;
            break;
         case XK_Control_L:
         case XK_Control_R:
            control_pressed = evt.type == KeyPress;
            break;
         case XK_Alt_L:
         case XK_Alt_R:
            alt_pressed = evt.type == KeyPress;
      }
      if(evt.type != KeyPress)
         return 0;

      return keyPressed;
   }

   void Handle(char *c) {
      if(who_ == EXECUTER)
         executer_handler_wrapper(c);
#ifdef PULSEAUDIO
      else if(who_ == PULSE)
         pulse_handler_wrapper();
#endif /* PULSEAUDIO */
#ifdef JACK_AUDIO
      else if(who_ == JACK)
         jack_handler_wrapper();
#endif /* JACK_AUDIO */
      else if(who_ == MOUSE)
         mouse_handler_wrapper(c);
      else {
        if(key_available()) {
           int keyPressed = get_key();
           if(keyPressed == XK_F14) {
             printf("F14 pressed\n");
           }
        }
         //XCheckTypedWindowEvent(disp, root, AnyPropertyType, &evt);
      }
   }

} keyboard_grabber;

int suffix(char *a, char *b) {
   char *p, *q;
   for(p = b; *p; p++);
   for(q = a; *q; q++, p--);
   return !strcasecmp(a,p);
}

#ifdef MUSICPLAYER

#define BASE_PATH (_("/home/edanaher/music/"))

int song_file_cmp(const void *av, const void *bv) {
   const char ***a = (const char ***)av;
   const char ***b = (const char ***)bv;
   return strcmp((*a)[1], (*b)[1]);
}

// TODO: weights, better randomization
class MusicPlayer {
   NiceOsd osd_;

   struct Song {
      char *name;
      char *file;
      char *artist;
      char *title;
      int length;
      int amp;
   };

   int lib_size_;
   Song library_[10000];

   int pl_size_;
   Song *playList_[10000];
   int playListWeights_[10000];
   int totalWeights_;

   int queue_size_;
   int selected_;
   Song *queue_[10000];

   xine_t *xine_;
   xine_stream_t *xine_stream_;
   xine_audio_port_t *xine_ao_port_;
   xine_event_queue_t *xine_ev_queue_;

   long long song_started_;
   Song *active_song_;
   int playing_;
   int update_song_started_;

   int nresults_;
   Song *results_[10000];
   int rotated_;

   int PipeRun(int fds[2], char *path, ...) {
      va_list args;
      char *argv[100];
      int i;
      va_start(args, path);
      argv[0] = path;
      for(i = 1; argv[i-1]; i++)
         argv[i] = va_arg(args, char *);
      va_end(args);
      int fdr[2], fdw[2];
      int pid;

      pipe(fdr);
      pipe(fdw);

      if(!(pid = fork())) {
         dup2(fdw[0], STDIN_FILENO);
         dup2(fdr[1], STDOUT_FILENO);
         close(fdw[1]);
         close(fdr[0]);
         execv(path, argv);
         exit(1);
      }

      close(fdw[0]);
      close(fdr[1]);
      fds[0] = fdr[0];
      fds[1] = fdw[1];
      return pid;
   }

   void LoadSongsFrom(char *path) {
      struct dirent **files;
      int i, j;
      int nfiles = scandir(path, &files, NULL, alphasort);

      for(i = 0; i < nfiles; i++) {
         if(!strcmp(files[i]->d_name, ".") || !strcmp(files[i]->d_name, ".."))
            goto free_files;
         if(files[i]->d_type == DT_DIR) {
            char *child = (char *)malloc(strlen(path) + strlen(files[i]->d_name) + 3);
            strcpy(child, path);
            strcat(child, files[i]->d_name);
            strcat(child, "/");
            LoadSongsFrom(child);
            free(child);
         }
         else if(suffix(_(".mp3"), files[i]->d_name) ||
                 suffix(_(".ogg"), files[i]->d_name) ||
                 suffix(_(".m4a"), files[i]->d_name)) {

            char *filename = (char *)malloc(strlen(path) + strlen(files[i]->d_name) + 3);
            strcpy(filename, path);
            strcat(filename, files[i]->d_name);

            TagLib::FileRef f(filename);
            char *artist = (char *)malloc(f.tag()->artist().to8Bit().length()+1);
            char *title = (char *)malloc(f.tag()->title().to8Bit().length()+1);
            strcpy(artist, f.tag()->artist().to8Bit().c_str());
            strcpy(title, f.tag()->title().to8Bit().c_str());
            int amp = 100;

            // Pull out the replaygain for vorbis, since xine doesn't.
            if(suffix(_(".ogg"), files[i]->d_name)) {
               double gain;
               char buffer[2024];
               int fd = open(filename, O_RDONLY);
               int len = read(fd, buffer, 1024);
               close(fd);

               // don't use strstr due to NULLs
               for(j = 0; j < len - 30; j++) {
                  if(!memcmp(buffer + j, "REPLAYGAIN_TRACK_GAIN=", 22))
                     break;
               }
               if(j != len - 30) {
                  sscanf(buffer + j + 22, "%lg", &gain);
                  amp = 100 * pow(2, gain / 6);
                  printf("%s: %g -> %d\n", filename, gain, amp);
               }
            }

            //delete f;
            int j;
            for(j = 0; title[j]; j++) {
               if((unsigned char)title[j] == 0xed)  {
                  title[j] = 'i';
               }
            }

            library_[lib_size_].file = filename;
            library_[lib_size_].title = title;
            library_[lib_size_].artist = artist;
            library_[lib_size_].length = 0;
            library_[lib_size_].amp = amp;
            if(title[0] && artist[0]) {
               // Central " - ", final " <" in queue, and null-termination
               library_[lib_size_].name = (char *)malloc(strlen(title) + strlen(artist) + 6);
               strcpy(library_[lib_size_].name, artist);
               strcat(library_[lib_size_].name, " - ");
               strcat(library_[lib_size_].name, title);

               if(strlen(library_[lib_size_].name) > 80) {
                 strcpy(library_[lib_size_].name, title);
                 library_[lib_size_].name[80] = 0;
               }
            } else {
               library_[lib_size_].name = stracpy(1+strrchr(library_[lib_size_].file, '/'));
            }
            lib_size_++;

         }
free_files:
         free(files[i]);
      }
      free(files);

   }

   void LoadSongs() {
      lib_size_ = 0;
      LoadSongsFrom(BASE_PATH);
   }

   void LoadPlaylist() {
      FILE *FIN = fopen("/home/edanaher/.fvwm/var/playlist", "r");
      char buffer[10000];
      int i, len, weight;

      pl_size_ = 0;
      if(!FIN) return;
      while(fgets(buffer, 10000, FIN)) {
         for(len = 0; buffer[len] && buffer[len] != '\n' &&
                      !(buffer[len] == ':' && buffer[len+1] == ':'); len++);
         if(buffer[len] == ':') {
            if(!sscanf(buffer + len + 2, "%d", &weight))
               weight = 1000;
         } else
            weight = 1000;
         buffer[len] = 0;
         for(i = 0; i < lib_size_; i++)
            if(!strcmp(library_[i].file, buffer))
               break;
         if(i == lib_size_) {
            printf("WARNING: Unknown file \"%s\"\n", buffer);
            continue;
         }
         totalWeights_ += weight;
         playListWeights_[pl_size_] = weight;
         playList_[pl_size_++] = &library_[i];
      }
   }

   void InitXine() {
      xine_ = xine_new();
      xine_config_load(xine_, "/home/edanaher/.xine/config");
      xine_init(xine_);
      xine_ao_port_ = xine_open_audio_driver(xine_, "auto", NULL);
      xine_stream_ = xine_stream_new(xine_, xine_ao_port_, NULL);
      xine_ev_queue_ = xine_event_new_queue(xine_stream_);
   }

   void GetPos(int *posp, int *timep, int *lenp) {
      int pos, time = 0, len = 0;
      int i;
      for(i = 10; i < 20 && !time; i++) {
         xine_get_pos_length(xine_stream_, &pos, &time, &len);
         usleep(1 << i);
      }
      if(posp)  *posp = pos;
      if(timep) *timep = time;
      if(lenp)  *lenp = len / 1000;
   }

   void PlaySong(Song *song) {
      char filename[1000];
      sprintf(filename, "file://%s", song->file);
      xine_open(xine_stream_, filename);
      xine_ao_port_->set_property(xine_ao_port_, AO_PROP_AMP, song->amp);
      printf("Setting amp to %d for %s\n", song->amp, song->file);
      xine_play(xine_stream_, 0, 0);

      active_song_ = song;
      struct timeval now;
      gettimeofday(&now, NULL);
      song_started_ = now.tv_sec * 1000 + now.tv_usec / 1000;
      update_song_started_ = 1;
      playing_ = 1;
      if(!song->length) {
         GetPos(NULL, NULL, &song->length);
      }
   }

   void PlayNext() {
      if(queue_size_) {
         PlaySong(queue_[0]);
         queue_size_--;
         memmove(queue_, queue_ + 1, queue_size_ * sizeof(queue_[0]));
      } else
         PlaySong(playList_[random() % pl_size_]);
   }

   void Pause() {
      static int pos;
      if(playing_) {
         playing_ ^= 3;
         if(playing_ == 1) {
            char filename[1000];
            sprintf(filename, "file://%s", active_song_->file);
            xine_open(xine_stream_, filename);
            xine_play(xine_stream_, 0, 0);
            xine_play(xine_stream_, 0, pos);
            //printf("unpausing: pos was %d\n", pos);
            update_song_started_ = 1;
            pos = 0;
            GetPos(NULL, &pos, NULL);
            //printf("unpaused: pos is %d\n", pos);
         }
         else {
            GetPos(NULL, &pos, NULL);
            //printf("pausing: pos is %d\n", pos); 
            xine_stop(xine_stream_);
         }
         song_started_ = now - song_started_;
      } else {
         printf("Pausing while not playing!\n");
      }
   }

   void Stop() {
      xine_stop(xine_stream_);
      playing_ = 0;
   }

   void Seek(int d) {
      int pos = 0, junk;
      while(!pos) {
         GetPos(NULL, &pos, NULL);
         usleep(1000);
      }
      pos += d * 1000;
      if(pos < 0) pos = 0;
      if(pos > active_song_->length*1000) { PlayNext(); return; }
      xine_play(xine_stream_, 0, pos);
      update_song_started_ = 1;
      song_started_ -= d * 1000;
      if(song_started_ > now)
         song_started_ = now;
      return;
   }


   void DrawSearch() {
      int i;
      //TODO: dedup
      UL_osd.Clear();
      for(i = 0 ; i < nresults_ && i < 10; i++) {
         UL_osd.WriteLine(results_[(i + rotated_) % nresults_]->name, i);
      }
   }

   void CloseSearch() {
      UL_osd.Clear();
      nresults_ = 0;
   }

   public:
   MusicPlayer() :
      osd_(XOSD_left, XOSD_bottom, 1),
      playing_(0),
      rotated_(0),
      queue_size_(0),
      totalWeights_(0),
      selected_(-1) {
      LoadSongs();
      LoadPlaylist();
      InitXine();
      if(pl_size_)
         active_song_ = playList_[random() % pl_size_];
   }


   void Update(int update) {
      char disp[1000];
      xine_event_t *xine_ev;

      while((xine_ev = xine_event_get(xine_ev_queue_))) {
         if(xine_ev->type == XINE_EVENT_UI_PLAYBACK_FINISHED) {
            int pos;
            GetPos(NULL, &pos, NULL);
            //printf("Finished a song: %d\n", pos);
            PlayNext();
         }
         xine_event_free(xine_ev);
      }

      if(playing_ == -1) {
         PlayNext();
      }

      if(update_song_started_ && playing_ == 1) {
         int junk, pos;
         xine_get_pos_length(xine_stream_, &junk, &pos, &junk);
         if(pos) {
            song_started_ = now - pos;
            update_song_started_ = 1;
         }

         //xine_get_pos_length(xine_stream_, &pos, &junk, &junk);
         //printf("updating time: pos is %d\n", pos);
      }

      int time = playing_ == 1 ? (now - song_started_) / 1000 : song_started_ / 1000;
      if(!playing_)
         disp[0] = 0;
      else
            sprintf(disp, "%s %d:%02d/%d:%02d", active_song_->name, time/60, time%60,
                                    active_song_->length/60, active_song_->length%60);
      char buffer[1000];
      if(update)
         osd_.Write(disp);
      else
         osd_.Hide();
   }

   void Handle(char *command) {
      int d;
      if(!strcmp(command, "play_pause")) {
         if(!playing_)
            PlaySong(active_song_);
         else
            Pause();
      }
      if(!strcmp(command, "next"))
         PlayNext();
      if(!strcmp(command, "stop"))
         Stop();
      if(sscanf(command, "seek %d", &d) == 1) 
         Seek(d);
   }

   int matches(char *haystack, char *needle) {
      char *cut;
      int final = 0;

      if(!strcmp(needle, "$"))
        return true;

      if(needle[strlen(needle)-1] == '$') {
         needle[strlen(needle)-1] = 0;
         final = 1;
      }

      while((cut = strchr(needle, '*')) && haystack) {
         *cut = 0;
         haystack = strstr(haystack, needle);
         if(haystack) haystack += strlen(needle);
         *cut = '*';
         needle = cut + 1;
      }
      if(final) {
         final = haystack && strstr(haystack, needle) == haystack + strlen(haystack) - strlen(needle);
         needle[strlen(needle)] = '$';
         return final;
      } else
         return haystack && strstr(haystack, needle);
   }

   void Search(char *pat) {
      int i;
      static char last_search[1000];

      while(*pat == '*')
         pat++;


      if(!strcmp(pat, last_search))
         return;

      /* No search */
      if(pat[0] == 0) {
         nresults_ = 0;
         UL_osd.Clear();
         return;
      }

      nresults_ = 0;
      rotated_ = 0;
      /* Global search */
      if(pat[0] == '@') {
         if(pat[1] == '/') {
            for(i = 0; i < lib_size_; i++)
               if(matches(library_[i].file, pat+2))
                  results_[nresults_++] = &library_[i];
         } else {
            for(i = 0; i < lib_size_; i++)
               if(matches(library_[i].name, pat+1))
                  results_[nresults_++] = &library_[i];
         }
      } else {
         if(pat[0] == '/') {
            for(i = 0; i < pl_size_; i++)
               if(matches(playList_[i]->file, pat+1))
                  results_[nresults_++] = playList_[i];
         } else {
            for(i = 0; i < pl_size_; i++)
               if(matches(playList_[i]->name, pat))
                  results_[nresults_++] = playList_[i];
         }
      }

      strcpy(last_search, pat);
      DrawSearch();
   }

   void RotateSelected(int n) {
      rotated_ += n;
      DrawSearch();
   }

   int PlaySelected() {
      if(nresults_ > 0)
         if(!queue_size_)
            PlaySong(results_[rotated_]);
         else {
            EnqueueSelected();
            return 1;
         }
      CloseSearch();
      return 0;
   }

   int EnqueueSelected(int n = -1) {
      if(nresults_ > 0) {
         if(n >= 0)
            queue_[queue_size_++] = results_[n];
         else
            queue_[queue_size_++] = results_[rotated_];
         DrawQueue(-1);
      }
      return 0;
   }
   
   void EnqueueAllSorted() {
      int i;
      int begin_sort = queue_size_;
      if(!nresults_)
         return;
      for(i = 0; i < nresults_; i++)
         queue_[queue_size_++] = results_[i];
      qsort(&queue_[begin_sort], queue_size_ - begin_sort,
            sizeof(queue_[0]), song_file_cmp);
      DrawQueue(-1);
   }

   void EnqueueRandom() {
      EnqueueSelected(random() % nresults_);
   }

   void DrawQueueElem(int n) {
      int len = strlen(queue_[n]->name);
      queue_[n]->name[len] = ' ';
      queue_[n]->name[len+1] = n == selected_ ? '<' : ' ';
      queue_[n]->name[len+2] = 0;
      UR_osd.WriteLine(queue_[n]->name, n);
      queue_[n]->name[len] = 0;
   }

   void DrawQueue(int n) {
      int i;

      if(n < 0) {
         UR_osd.Clear();
         if(n < -1 && !queue_size_)
            UR_osd.WriteLine(_("No queue"), 0);
         for(i = 0; i < queue_size_; i++) 
            DrawQueueElem(i);
      } else
         DrawQueueElem(n);
   }

   void SelectQueue(int pos) {
      int oldselected = selected_;
      if(pos == selected_ || pos >= queue_size_)
         return;
      selected_ = pos;
      if(oldselected != -1);
         DrawQueue(oldselected);
      if(selected_ != -1);
         DrawQueue(selected_);
   }

   void RandomizeQueue() {
      int i;
      for(i = 0; i < queue_size_; i++) {
         int j = i + random() % (queue_size_ - i);
         Song *tmp = queue_[i];
         queue_[i] = queue_[j];
         queue_[j] = tmp;
      }
      DrawQueue(-2);
   }

   void AdjustQueue(int k) {
      int delta = 0;
      int move = 0, top;

      int i;

      Song *tmp;

      if(!queue_size_)
         return;
      if(k == 'r') { RandomizeQueue(); return; }
      if(k == 'j') delta = 1;
      if(k == 'k') delta = queue_size_ - 1;
      if(k == 'f') move = -1;
      if(k == 'd') move = 1;
      if(delta)
         SelectQueue((selected_ + delta) % queue_size_);
      if(move && move + selected_ >= 0 && move + selected_ < queue_size_) {
         int target = selected_ + move;
         Song *tmp = queue_[selected_];
         queue_[selected_] = queue_[target];
         queue_[target] = tmp;
         SelectQueue(target);
      } else if(move) {
         top = (move == -1) ? 1 : 0;
         Song *tmp = queue_[move == -1 ? 0 : queue_size_ - 1];
         memmove(queue_ + top + move, queue_ + top, sizeof(queue_[0]) * (queue_size_ - 1));
         queue_[move == -1 ? queue_size_ - 1 : 0] = tmp;
         SelectQueue(move == -1 ? queue_size_ - 1 : 0);
         DrawQueue(-2);
      }
      if(k == 'x') {
         memmove(&queue_[selected_], &queue_[selected_ + 1],
               sizeof(queue_[0]) * (queue_size_ - selected_ - 1));
         queue_size_--;
         if(selected_ == queue_size_)
            selected_--;
         SelectQueue(selected_);
         DrawQueue(-2);
      }
   }

   int QueueEmpty() {
      return !queue_size_;
   }

   void SavePlaylist() {
      int i;
      FILE *FOUT = fopen("/home/edanaher/.fvwm/var/playlist", "w");
      for(i = 0; i < pl_size_; i++)
         fprintf(FOUT, "%s::%d\n", playList_[i]->file, playListWeights_[i]);
      fclose(FOUT);
   }

   void AddToPlaylist(int all) {
      if(!all) {
         totalWeights_ += 1000;
         playListWeights_[pl_size_] = 1000;
         playList_[pl_size_++] = results_[rotated_];
      } else {
         int i;
         for(i = 0; i < nresults_; i++) {
            totalWeights_ += 1000;
            playListWeights_[pl_size_] = 1000;
            playList_[pl_size_++] = results_[i];
         }
      }

      SavePlaylist();
   }

   void RemoveFromPlaylist() {
      int i;
      for(i = 0; i < pl_size_; i++)
         if(playList_[i] == results_[rotated_])
            break;
      if(i == pl_size_)
         return;
      totalWeights_ -= playListWeights_[i];
      for(; i < pl_size_; i++) {
         playListWeights_[i] = playListWeights_[i + 1];
         playList_[i] = playList_[i + 1];
      }
      pl_size_--;
      SavePlaylist();

      for(i = rotated_; i < nresults_; i++)
         results_[i] = results_[i+1];
      nresults_--;
      DrawSearch();
   }

   void SavePlaylistState() {
      FILE *FOUT = fopen("/home/edanaher/.fvwm/var/playliststate", "w");
      int i;
      int pos;
      GetPos(&pos, NULL, NULL);
      fprintf(FOUT, "%s\n%d %lld\n", active_song_->file, playing_, (long long)pos);
      fprintf(FOUT, "%d\n", queue_size_);
      for(i = 0; i < queue_size_; i++)
         fprintf(FOUT, "%s\n", queue_[i]->file);
      fclose(FOUT);
   }

   Song *LookupByFile(char *file) {
      int i;
      for(i = 0; i < lib_size_; i++)
         if(!strcmp(library_[i].file, file))
            break;
      if(i == lib_size_)
         return NULL;
      return &library_[i];
   }

   void LoadPlaylistState() {
      FILE *FIN = fopen("/home/edanaher/.fvwm/var/playliststate", "r");
      int i;
      char buffer[1000];
      Song *tmp;
      int playing;
      int pos;

      if(!FIN) return;

      fgets(buffer, 1000, FIN);
      buffer[strlen(buffer)-1] = 0;
      tmp = LookupByFile(buffer);
      fscanf(FIN, "%d %d", &playing, &pos);

      if(tmp) {
         PlaySong(tmp);
         playing_ = playing;
         if(!playing)
            Stop();
         if(playing == 1) {
            xine_play(xine_stream_, pos, 0);
            update_song_started_ = 1;
         }
         if(playing == 2)
            Pause();
      }

      fscanf(FIN, "%d\n", &queue_size_);
      for(i = 0; i < queue_size_; i++) {
         fgets(buffer, 1000, FIN);
         buffer[strlen(buffer)-1] = 0;
         queue_[i] = LookupByFile(buffer);
         if(!queue_[i])
            queue_size_ = i;
      }
      fclose(FIN);
   }
} music_player;

void SavePlaylistStateWrapper() {
   music_player.SavePlaylistState();
}
#endif /* MUSICPLAYER */

class MouseCtrl {
   Display *disp_;
   Window root_;
   int count_, counting_;
   int last_key_, last_count_;
   int active_btn_;
   int poses_[12][2];

   void MouseTwiddle(int state, int b) {
      XTestFakeButtonEvent(disp_, b, state ? True : False, CurrentTime);
      XFlush(disp_);
   }

   void Click(int b) {
      MouseTwiddle(1, b);
      usleep(5000);
      MouseTwiddle(0, b);
   }

   void MoveMouse(int dx, int dy) {
      XWarpPointer(disp_, None, None, 0, 0, 0, 0, dx, dy);
      XFlush(disp_);
   }

   void MoveMouseTo(int x, int y) {
      XWarpPointer(disp_, None, root_, 0, 0, 0, 0, x, y);
      XFlush(disp_);
   }


   public:

   int active;

   void run() {
      static long long lastrun = 0;
      if(now - lastrun < 200)
         return;
      lastrun = now;
      Click(active_btn_);
   }

   MouseCtrl() :
      disp_(XOpenDisplay(NULL)),
      root_(RootWindow(disp_, DefaultScreen(disp_))),
      count_(1),
      counting_(0),
      active(0)
   {
      int i;
      for(i = 0; i < 12; i++)
         poses_[i][0] = poses_[i][1] = 100;
   }

   void doKey(int keyPressed) {
      int i;

      switch(keyPressed) {
         case XK_Escape:
         case 'q':
            keyboard_grabber.release();
            active = 0;
            break;
         case 'D': active = 1; active_btn_ = 1; break;
         case 'd': Click(1); break;
         case 'c': MouseTwiddle(1, 1); break;
         case 'e': MouseTwiddle(0, 1); break;

         case 'F': active = 1; active_btn_ = 3; break;
         case 'f': Click(3); break;
         case 'v': MouseTwiddle(1, 3); break;
         case 'r': MouseTwiddle(0, 3); break;

         case 'S': active = 1; active_btn_ = 2; break;
         case 's': Click(2); break;
         case 'x': MouseTwiddle(1, 2); break;
         case 'w': MouseTwiddle(0, 2); break;

         case 'a': active = 0; break;

         case 't': for(i = 0; i < count_; i++) Click(4); break;
         case 'g': for(i = 0; i < count_; i++) Click(5); break;

         case 'h': MoveMouse(-count_, 0); break;
         case 'j': MoveMouse(0, count_); break;
         case 'k': MoveMouse(0, -count_); break;
         case 'l': MoveMouse(count_, 0); break;

         case '.': int old_count_ = count_;
                   count_ *= last_count_;
                   doKey(last_key_);
                   count_ = last_count_ / old_count_;
      }

      if(XK_F1 <= keyPressed && keyPressed <= XK_F12) {
         if(keyboard_grabber.control_pressed) {
            poses_[keyPressed - XK_F1][0] = keyboard_grabber.x;
            poses_[keyPressed - XK_F1][1] = keyboard_grabber.y;
         } else if(keyboard_grabber.shift_pressed) {
            MoveMouseTo(poses_[keyPressed - XK_F1][0],
                        poses_[keyPressed - XK_F1][1]);
         } else {
            MoveMouseTo(poses_[keyPressed - XK_F1][0],
                        poses_[keyPressed - XK_F1][1]);
            usleep(50000);
            Click(1);
            usleep(50000);
            MoveMouseTo(keyboard_grabber.x, keyboard_grabber.y);
         }
      }

      if('0' <= keyPressed && keyPressed <= '9') {
         count_ = (count_ - !counting_) * 10 + keyPressed - '0';
         counting_ = 1;
         if(count_ > 2000)
            count_ = 2000;
      } else {
         counting_ = 0;
         last_count_ = count_;
         count_ = 1;
         if(keyPressed != '.')
            last_key_ = keyPressed;
      }
   }

   void Handle(char *c) {

      int keyPressed = 0;
      if(keyboard_grabber.key_available())
         keyPressed = keyboard_grabber.get_key();
      if(keyPressed)
         doKey(keyPressed);
   }
} mouse_ctrl;

void oopsies(char *line) {
   perror("Line finished!\n");
}

void wrapper(char **, int, int);

class Executer {
   NiceOsd osd_;
#define osd_out_ UL_osd
#define osd_err_ UR_osd
   enum {EXEC,
        DC,
        JCONSOLE,
#ifdef MUSICPLAYER
        MUSIC,
        PLAYLIST
#endif /* MUSICPLAYER */
   } mode_;
   int fds_[2];

   public:
   int reading_line;

   Executer() :
      osd_(XOSD_center, XOSD_bottom, 1),
      reading_line(0)
   {
      osd_out_.SetTimeOut(5);
      osd_err_.SetTimeOut(5);
      osd_out_.SetFont(_("-*-bitstream vera sans mono-bold-r-*-*-*-160-*-*-*-*-*-*"), _("#40FFFF"), _("#000040"), 2);
      osd_err_.SetFont(_("-*-bitstream vera sans mono-bold-r-*-*-*-160-*-*-*-*-*-*"), _("#40FFFF"), _("#000040"), 2);
   }

   void HandleTabs(char **matches, int numMatches, int maxLen) {
      char output[1000];
      char *p;
      int i, j, choplen;

      printf("Handling tab\n");
      for(i = strlen(matches[0]) - 1; i > 0 && matches[0][i] != '/'; i--);
      if(matches[0][i] == '/')
         i++;
      choplen = i;
      maxLen = 0;
      for(i = 0; i < numMatches; i++)
         if(strlen(matches[i]) > maxLen)
            maxLen = strlen(matches[i]);

      p = output;
      for(i = 1; i < min(numMatches + 1, 10); i++) {
         for(j = 0; matches[i][j + choplen]; j++)
            *(p++) = matches[i][j + choplen];
         *(p++) = '\n';
      }
      *p = 0;
      printf("Writing output:\n==============\n%s\n==============\n", output);
      osd_out_.Write(output);
   }

   void write_char(char c) {
      write(fds_[1], &c, 1);
      rl_callback_read_char();
   }

   void Handle(char *c) {
      char tmp[1000];
      int keyPressed = 0, keySym = 0;
      char buf = 0;

      if(c && !strcmp(c, ".show_osd")) {
         osd_out_.Show();
         return;
      }

      if(c && !reading_line) {
         keyboard_grabber.grab(keyboard_grabber.EXECUTER);

         // Initialize rl.
         pipe(fds_);
         rl_instream = fdopen(fds_[0], "r");
         rl_outstream = fopen("/home/edanaher/.fvwm/osd_out_pipe_note", "w");
         //rl_completion_display_matches_hook = wrapper;
         rl_complete(0, '	');
         rl_callback_handler_install("", oopsies);
         if(!strcmp(c, "calc")) {
            mode_ = DC;
            osd_.Write(_("=#"));
         } else if(!strcmp(c, "jconsole")) {
            mode_ = JCONSOLE;
            osd_.Write(_("-#"));
         } else if(!strcmp(c, "start")) {
            mode_ = EXEC;
            osd_.Write(_(":#"));
         } else {
#ifdef MUSICPLAYER
            if(!strcmp(c, "music")) {
               mode_ = MUSIC;
               music_player.SelectQueue(-1);
               music_player.DrawQueue(-1);
               osd_.Write(_("^#"));
            } else {
               mode_ = PLAYLIST;
               music_player.SelectQueue(0);
               music_player.DrawQueue(-2);
            }
            osd_out_.SetTimeOut(-1);
            osd_err_.SetTimeOut(-1);
#endif /* MUSICPLAYER */
         }
         write_char('#');
         reading_line = 1;
      } else if(reading_line && keyboard_grabber.key_available()) {
         keyPressed = keyboard_grabber.get_key();
         if(!keyPressed)
            return;

#ifdef MUSICPLAYER
         if(keyPressed == XK_backslash && mode_ == MUSIC) {
            music_player.EnqueueSelected();
            return;
         }
#endif /* MUSICPLAYER */

         if(keyPressed == XK_Return) {
            write_char('');
            add_history(rl_line_buffer);
            if(mode_ == DC) {
               sprintf(tmp, "echo '%sp' | dc | awk 'BEGIN {ORS=\"\"; RS=\"\\\\\"}  {print $1}'", rl_line_buffer);
               Run(tmp);
            }
            else if(mode_ == JCONSOLE) {
               sprintf(tmp, "echo '%s' | ~edanaher/j503a/jconsole | sed 's/^ *//'", rl_line_buffer);
               Run(tmp);
            }
            else if(mode_ == EXEC) {
               Run(rl_line_buffer);
            }
#ifdef MUSICPLAYER
            else {
               if(music_player.PlaySelected())
                  return;
               osd_out_.SetTimeOut(5);
               osd_out_.Hide();
            }
#endif /* MUSICPLAYER */
            goto finish;
         }

         if(keyPressed == XK_Escape ||
            keyPressed == XK_c && keyboard_grabber.control_pressed == 1) {
         finish:
            rl_callback_handler_remove();
            fclose(rl_outstream);
            close(fds_[0]);
            close(fds_[1]);
            osd_.Hide();
            keyboard_grabber.release();
            reading_line = 0;
#ifdef MUSICPLAYER
            if(mode_ == MUSIC || mode_ == PLAYLIST) {
               osd_out_.SetTimeOut(5);
               osd_out_.Hide();
               osd_err_.SetTimeOut(5);
               osd_err_.Hide();
            }
#endif /* MUSICPLAYER */
            return;
         }

         if('a' <= keyPressed && keyPressed <= 'z' &&
            keyboard_grabber.control_pressed)
            keyPressed &= 0x1F;

#ifdef MUSICPLAYER
         if(mode_ == MUSIC && keyPressed == XK_Tab) {
            /* If empty, switch to queue */
            if(rl_line_buffer[1] == 0 && !music_player.QueueEmpty()) {
               music_player.SelectQueue(0);
               music_player.DrawQueue(-2);
               mode_ = PLAYLIST;
            } else
               music_player.RotateSelected(1);
            keyPressed = 0;
         }

         if(mode_ == PLAYLIST) {
            if(keyPressed == XK_Tab) {
               music_player.SelectQueue(-1);
               music_player.DrawQueue(-1);
               mode_ = MUSIC;
            }
            music_player.AdjustQueue(keyPressed);
            keyPressed = 0;
         }
#endif /* MUSICPLAYER */

         int arrow_pressed = 0;
         switch(keyPressed) {
            case XK_Tab:
               //rl_complete_internal('!');
               //keyPressed = 0;
            case XK_BackSpace:
               keyPressed &= 0xFF;
               break;
#ifdef MUSICPLAYER
            case 'A':
               if(mode_ == MUSIC)
                  if(keyboard_grabber.control_pressed) {
                     music_player.AddToPlaylist(1); //all
                     keyPressed = 0;
                  }
               break;
            case 'L':
               if(mode_ == MUSIC)
                  if(keyboard_grabber.control_pressed) {
                     music_player.EnqueueAllSorted();
                     keyPressed = 0;
                  }
               break;
            case 'X':
               if(mode_ == MUSIC)
                  if(keyboard_grabber.control_pressed) {
                     music_player.RemoveFromPlaylist();
                     keyPressed = 0;
                  }
               break;
            case '`':
               if(mode_ == MUSIC) {
                  write_char('');
                  music_player.EnqueueRandom();
                  write_char('#');
               }
               keyPressed = 0;
               break;
#endif /* MUSICPLAYER */
            case XK_Up: keyPressed += 1;
            case XK_Down: keyPressed -= 2;
            case XK_Right: keyPressed -= 3;
            case XK_Left: keyPressed -= 13;
               keyPressed &= 0xFF;
               arrow_pressed = 1;
         }
         /* Added the first condition - stuff might break 2010-02-15
          * This also means the last condition is redundant */
         if(keyPressed && keyPressed < 256 && keyPressed != '' &&
               keyPressed != ''
#ifdef MUSICPLAYER
               && !(mode_ == MUSIC && keyPressed == '`')
#endif /* MUSICPLAYER */
               ) {
            write_char('');
            if(keyboard_grabber.alt_pressed)
               write_char('');
            if(arrow_pressed) {
               write_char('');
               write_char('[');
            }
            write_char(keyPressed);
#ifdef MUSICPLAYER
            if(mode_ == MUSIC)
              music_player.Search(rl_line_buffer);
#endif /* MUSICPLAYER */
            write_char('#');
         }

         sprintf(tmp, "%c%s\n", prompts[(int)mode_], rl_line_buffer);
         osd_.Write(tmp);
      }
      return;
   }

   int Run(char *com) {
      char command[1000];
      close(fds_[0]);
      close(fds_[1]);
      rl_callback_handler_remove();

      if(!fork()) {
         close(STDOUT_FILENO);
         open("/home/edanaher/.fvwm/osd_out_pipe", O_WRONLY);
         close(STDERR_FILENO);
         open("/home/edanaher/.fvwm/osd_err_pipe", O_WRONLY);
         sprintf(command, "%s &", com);
         system(command);
         exit(0);
      }
   }

   int Output(char * out) {
      osd_out_.Append(out);
   }
   
   int Error(char * out) {
      osd_err_.Append(out);
   }
#undef osd_out_
#undef osd_err_
} executer;

#ifdef JACK_AUDIO
void __jack_error(const char *);
int __jack_process(jack_nframes_t, void *);
void *__jack_unregister_in_another_thread(void *);
void *__jack_disconnect_in_another_thread(void *);
void *__jack_harmonize_in_another_thread(void *);
void __jack_port_connected(jack_port_id_t, jack_port_id_t, int, void *);
void __jack_client_registered(const char *, int, void *);
void __jack_port_registered(jack_port_id_t, int, void *);


#define JACK_DEBUG
#ifdef JACK_DEBUG
int jack_debug_indent[100];
int njack_thread_ids = 0;
int jack_thread_ids[100];
int jack_thread_id() {
   int i;
   int tid = (int)pthread_self();
   for(i = 0; i < njack_thread_ids; i++)
      if(jack_thread_ids[i] == tid)
         return i;
   jack_thread_ids[njack_thread_ids] = tid;
   jack_debug_indent[njack_thread_ids] = 0;
   return njack_thread_ids++;
}
int colors[] = {7, 196, 34, 214, 62, 160, 112, 68};
#define jack_debug(n, s, ...) \
     for(int __x__ = 0; __x__ < jack_debug_indent[jack_thread_id()]; __x__++) printf(" "); \
     printf("[33m%u JACK DEBUG %d:%d [0m: [38;5;%dm" s "[0m\n",  jack_thread_id(), apps_[0].nports[0], apps_[0].nports[1], colors[jack_thread_id() % (sizeof(colors) / sizeof(colors[0]))], ##__VA_ARGS__); \
     jack_debug_indent[jack_thread_id()] += n
#define return_debug(n) { jack_debug_indent[jack_thread_id()] += n; return; }
#define jack_debug_indent(n) jack_debug_indent[jack_thread_id()] += n
#else
#define jack_debug(...) 
#define jack_debug_indent(...)
#endif
#define PER_APP_JACK_NAME "per-app volume control"
class JackCtrl {
   jack_client_t *client_;
   const char *hardware_port_names_[2];
   jack_port_t * hardware_ports_[2];
   int selected_;
   int next_port_num_;
   typedef enum { PORT_NONE, PORT_CONNECT, PORT_DISCONNECT} active_event_types_t;
   active_event_types_t active_event_type_;
   const char *active_event_ports_[2];

   struct _app {
      const char *ports[2][4];
      jack_port_t *pipes[2][4][2];
      int nports[2];
      const char *jack_name;
      const char *name;
      int pid;
      int volume;
      float volume_multiplier;
      int muted;
   } apps_[20];
   int napps_;

   struct __jack_disconnect_args { const char *port1; const char *port2; };


   public:

   JackCtrl() {
      selected_ = -1;
      next_port_num_ = 0;
      active_event_type_ = PORT_NONE;

      printf("Initializing Jack\n");
      jack_set_error_function(__jack_error);

      if(!(client_ = jack_client_open(PER_APP_JACK_NAME, JackNoStartServer, NULL))) {
         fprintf(stderr, "failed to create new JACK client");
         return;
      }

      jack_set_port_connect_callback(client_, __jack_port_connected, NULL);
      jack_set_client_registration_callback(client_, __jack_client_registered, NULL);
      jack_set_port_registration_callback(client_, __jack_port_registered, NULL);
      jack_set_process_callback(client_, __jack_process, NULL);

      hardware_port_names_[0] = "system:playback_1";
      hardware_port_names_[1] = "system:playback_2";
      hardware_ports_[0] = jack_port_by_name(client_, hardware_port_names_[0]);
      hardware_ports_[1] = jack_port_by_name(client_, hardware_port_names_[1]);

      if(jack_activate(client_)) {
         fprintf(stderr, "Error activating client\n");
         client_ = NULL; //TODO: free?)
         return;
      }

      const char **existing_output_ports[2];
      existing_output_ports[0] = jack_port_get_connections(hardware_ports_[0]);
      existing_output_ports[1] = jack_port_get_connections(hardware_ports_[1]);

      napps_ = 0;
      int i, j;
      if(existing_output_ports[0])
         for(i = 0; existing_output_ports[0][i]; i++)
            AppForPort(existing_output_ports[0][i], 0, 1);
      if(existing_output_ports[1])
         for(i = 0; existing_output_ports[1][i]; i++)
            AppForPort(existing_output_ports[1][i], 1, 1);
   }

   void ErrorHandler(const char *desc) {
      jack_debug(0, "Error: [31;1m%s[0m", desc);
   }

   void JackClientRegistered(const char *name, int connect, void *arg) {
      jack_debug(0, "CB Client %s %sregistered", name, connect ? "" : "un");
      //printf("%s %sconnected\n", name, connect ? "" : "un");
   }

   void *UnregisterInAnotherThread(void *port) {
      jack_port_unregister(client_, (jack_port_t *)port);
      return NULL;
   }

   void *DisconnectInAnotherThread(void *args_v) {
      __jack_disconnect_args *args = (__jack_disconnect_args *)args_v;
      jack_debug(0, "Disconnecting in another thread: %s <-> %s", args->port1, args->port2);
      jack_disconnect(client_, args->port1, args->port2);
      free(args);
   }

   void *HarmonizeInAnotherThread(void *app) {
      HarmonizeApp((_app *)app);
      return NULL;
   }

   int JackProcess(jack_nframes_t nframes, void *arg) {
     int ch, i, j, k;
     jack_default_audio_sample_t *in;
     jack_default_audio_sample_t *out;
     for(i = 0; i < napps_; i++)
        if(apps_[i].volume != 0 && !apps_[i].muted) {
           //jack_debug(0, "Processing %s with %f", apps_[i].name, apps_[i].volume_multiplier);
           for(ch = 0; ch < 2; ch++)
              for(j = 0; j < apps_[i].nports[ch]; j++) {
                 if(!apps_[i].pipes[ch][j][0] || !apps_[i].pipes[ch][j][1])
                    continue;
                 in = (jack_default_audio_sample_t *)(jack_port_get_buffer(apps_[i].pipes[ch][j][0], nframes));
                 out = (jack_default_audio_sample_t *)(jack_port_get_buffer(apps_[i].pipes[ch][j][1], nframes));
                 memcpy(out, in, sizeof(jack_default_audio_sample_t) * nframes); for(k = 0; k < nframes; k++)
                    out[k] *= apps_[i].volume_multiplier;
              }
        }
     return 0;
   }

   void JackPortConnected(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
      const char *names[2] = { jack_port_name(jack_port_by_id(client_, a)),
                               jack_port_name(jack_port_by_id(client_, b)) };
      int channel, which, i, j;
      jack_debug(2, "CB Port %sconnected (%s <-> %s)", connect ? "" : "dis", names[0], names[1]);
      active_event_type_ = connect ? PORT_CONNECT : PORT_DISCONNECT;
      active_event_ports_[0] = names[0];
      active_event_ports_[1] = names[1];
      _app *app = NULL;
      for(i = 0; i < 2; i++)
         for(j = 0; j < 2; j++)
            if(!strcmp(names[1-i], hardware_port_names_[j]) &&
               strncmp(names[i], PER_APP_JACK_NAME, strlen(PER_APP_JACK_NAME))) {
               app = AppForPort(names[i], j, connect);
               which = i;
               channel = j;
            }
      jack_debug(0, "app is %p", app);
      if(app == NULL) {
         jack_debug(0, "Ignoring CB Port %sconnected (%s <-> %s)", connect ? "" : "dis", names[0], names[1]);
         return_debug(-2);
      }
      jack_debug(0, "app is %s", app->name);

      //pthread_t tid;
      //pthread_create(&tid, NULL, __jack_harmonize_in_another_thread, app);

      if(!connect && !app->muted && !app->volume) {
         jack_debug(0, "Attempting to remove connection %s from %s", names[which], names[1-which]);
         for(i = 0; i < app->nports[channel]; i++)
            if(!strcmp(app->ports[channel][i], names[which]))
               break;
         if(i == app->nports[channel]) {
            jack_debug(0, "JackCtrl error: Didn't find channel on disconnect\n");
            return_debug(-2);
         }
         jack_debug(0, "Disconnecting %s (%s:%d:%d)", names[which], app->name, channel, i);
         //jack_free((void *)app->ports[channel][i]);
         KillPort(app, channel, i);

         Reap();
      }
      /*else if(!connect && !app->muted && app->volume) {
         jack_debug(0, "Disconnect from volumed channel: %s", names[which]);
      }*/ else
         HarmonizePort(app, channel, which);

      active_event_type_ = PORT_NONE;
      return_debug(-2);

      if(connect == 1 && app->muted) {
         jack_disconnect(client_, names[which], names[1-which]);
         printf("Auto-muting %s\n", names[which]);
      }

      if(connect == 1 && app->volume && !app->muted) {
         jack_debug(0, "Auto-vol-ing %s", names[which]);
         ConnectPort(app, channel, which, 0, 0);
         ConnectPort(app, channel, which, 1, 1);
      }

      if(connect == 0 && !app->muted && !app->volume) {
         jack_debug(0, "Attempting to disconnect %s from %s", names[which], names[1-which]);
         for(i = 0; i < app->nports[channel]; i++)
            if(!strcmp(app->ports[channel][i], names[which]))
               break;
         if(i == app->nports[channel]) {
            //fprintf(stderr, "JackCtrl error: Didn't find channel on disconnect\n");
            return_debug(-2);
         }
         jack_debug(0, "Disconnecting %s (%s:%d:%d)", names[which], app->name, channel, i);
         //jack_free((void *)app->ports[channel][i]);
         KillPort(app, channel, i);

         Reap();
      }
      jack_debug(-2, "End CB Port %sconnected (%s <-> %s)", connect ? "" : "dis", names[0], names[1]);
   }

   void JackPortRegistered(jack_port_id_t id, int connect, void *arg) {
      const char *name = jack_port_name(jack_port_by_id(client_, id));
      int ch, channel, i, j, which;
      jack_debug(2, "CB Port %sregistered: %s", connect ? "" : "un", name);
      _app *app = AppForPort(name, -1, 0);

      if(!app) return_debug(-2);
      if(connect) return_debug(-2);
      jack_debug(0, "Attempting to remove port %s", name);

      channel = -1;
      for(ch = 0; ch < 2; ch++) {
         for(i = 0; i < app->nports[ch]; i++)
            if(!strcmp(app->ports[ch][i], name)) {
               channel = ch;
               which = i;
            }
      }
      if(channel == -1)
         //fprintf(stderr, "JackCtrl error: Didn't find channel on disconnect\n");
         return_debug(-2);

      KillPort(app, channel, which);

      Reap();
      jack_debug_indent(-2);
   }

   void Reap() {
      int i, j, ch;
      jack_debug(2, "Reaping");
      for(i = 0; i < napps_; i++)
         if(!AppAlive(&apps_[i])) {
            //jack_free((char *)app->jack_name);
            //free((char *)app->name);
            printf("Deleting app: %s (%s)\n", apps_[i].name, apps_[i].jack_name);

            for(ch = 0; ch < 2; ch++)
               while(apps_[i].nports[ch])
                  KillPort(&apps_[i], ch, 0);

            for(j = i + 1; j < napps_; j++)
               apps_[j - 1] = apps_[j];
            napps_--;
            i--;
         }
      jack_debug_indent(-2);
   }

   void Handle() {
      int i;
      char line[150];

      Reap();
      if(selected_ == -1) {
         if(!napps_) {
            UL_osd.Write(_("No Jack inputs"));
            return;
         }
         selected_ = 0;
         keyboard_grabber.grab(keyboard_grabber.JACK);
      }

      int keyPressed = 0;
      if(keyboard_grabber.key_available())
         keyPressed = keyboard_grabber.get_key();
      if(keyPressed) {
         if(keyPressed == XK_Escape || keyPressed == XK_Return ||
            keyPressed == XK_c && keyboard_grabber.control_pressed == 1) {
            selected_ = -1;

            UL_osd.Hide();
            UL_osd.SetTimeOut(5);
            keyboard_grabber.release();
            return;
         }
         if('0' == keyPressed) SetVolume(&apps_[selected_], 0);
         if('1' <= keyPressed && keyPressed <= '9')
            SetVolume(&apps_[selected_], -3 * (10 - (keyPressed - '0')));
         if(keyPressed == 'd')
            SetVolume(&apps_[selected_], apps_[selected_].volume - 1);
         if(keyPressed == 'f')
            SetVolume(&apps_[selected_], apps_[selected_].volume + 1);
         if(keyPressed == 'm') Mute(&apps_[selected_]);
         if(keyPressed == 'j') selected_++;
         if(keyPressed == 'k') selected_--;

         char *selectors = _("qwertyuiop");
         if(strchr(selectors, keyPressed))
            selected_ = strchr(selectors, keyPressed) - selectors;
         selected_ = (selected_ + napps_) % napps_;
      }

      UL_osd.SetTimeOut(-1);
      UL_osd.Clear();
      for(i = 0; i < napps_; i++) {
         snprintf(line, 145, "%c%c%c%+3d %s\n", i == selected_ ? '>' : ' ',
               apps_[i].nports[0] || apps_[i].nports[1] ? ' ' : '.',
               apps_[i].muted ? 'M' : ' ',
               apps_[i].volume, apps_[i].name);
         UL_osd.WriteLine(line, i);
      }

   }

   void Quit() {
      jack_debug(2, "Exiting");
      jack_client_close(client_);
      jack_debug_indent(-2);
   }

   private:

   void HarmonizePort(_app *app, int ch, int i) {
      jack_debug(2, "Harmonizing %s:%d:%d (%s)", app->name, ch, i, app->ports[ch][i]);
      int j;
      const char *pipename[2];
      jack_port_t * port = jack_port_by_name(client_, app->ports[ch][i]);
      jack_debug(0, "Port is %p", port);
      const char **connections = jack_port_get_connections(port);
      char port_name[100];
      jack_debug(0, "Harmonizing 1");

      int hardware_connect = !app->muted && !app->volume;
      int pipe_connect = !app->muted && app->volume;
      active_event_types_t state_changing = PORT_NONE;
      if(!strcmp(active_event_ports_[0], app->ports[ch][i]) &&
         !strcmp(active_event_ports_[1], hardware_port_names_[ch]))
         state_changing = active_event_type_;
      jack_debug(0, "Port status: %s <-> %s: %d", active_event_ports_[0], active_event_ports_[1],
                                                  active_event_type_);

      jack_debug(0, "Harmonizing 2");
      if(hardware_connect) {
         jack_debug(0, "Harmonizing 2a");
         if(connections)
            for(j = 0; connections[j]; j++)
               if(!strcmp(connections[j], hardware_port_names_[ch]))
                  break;
         jack_debug(0, "Harmonizing 2b");
         // no connections or didn't find HW connection, and not being connected
         if((!connections || !connections[j]) && state_changing != PORT_CONNECT) {
            jack_debug(0, "Connecting %s to hardware", app->ports[ch][i]);
            jack_connect(client_, app->ports[ch][i], hardware_port_names_[ch]);
            jack_debug(0, "Connected %s to hardware", app->ports[ch][i]);
         }
         jack_debug(0, "Harmonizing 2c");
      } else {
         jack_debug(0, "Harmonizing 2d");
         if(connections)
            for(j = 0; connections[j]; j++) {
               jack_debug(0, "Harmonizing 2e");
               if(!strcmp(connections[j], hardware_port_names_[ch]) && state_changing != PORT_DISCONNECT) {
                  jack_debug(0, "Disconnecting %s <-> %s", app->ports[ch][i], hardware_port_names_[ch]);
                  pthread_t tid;
                  __jack_disconnect_args *args = (__jack_disconnect_args *)malloc(sizeof(__jack_disconnect_args));
                  args->port1 = app->ports[ch][i];
                  args->port2 = hardware_port_names_[ch];
                  pthread_create(&tid, NULL, __jack_disconnect_in_another_thread, args);
               }
            }
         jack_debug(0, "Harmonizing 2f");
      }
      jack_debug(0, "Harmonizing 3");

      if(pipe_connect) {
         if(!app->pipes[ch][i][0]) {
            jack_debug(2, "Creating pipe for [38;5;93m%s:%d:%d[0m; %s", app->name, ch, i, app->ports[ch][i]);
            sprintf(port_name, "input %d [%d]", next_port_num_, app->pid);
            app->pipes[ch][i][0] = jack_port_register(client_, port_name,
                   JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
            sprintf(port_name, "output %d [%d]", next_port_num_, app->pid);
            app->pipes[ch][i][1] = jack_port_register(client_, port_name,
                   JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            next_port_num_++;
            jack_debug(0, "Pipe created: %p:%p", app->pipes[ch][i][0], app->pipes[ch][i][1]);

            jack_connect(client_, app->ports[ch][i], jack_port_name(app->pipes[ch][i][0]));
            jack_connect(client_, jack_port_name(app->pipes[ch][i][1]), hardware_port_names_[ch]);
            jack_debug(0, "Created pipe for %s:%d:%d; %s", app->name, ch, i, app->ports[ch][i]);
            jack_debug_indent(-2);
         }
      } else {
         if(app->pipes[ch][i][0]) {
            pipename[0] = jack_port_name(app->pipes[ch][i][0]);
            for(j = 0; connections[j]; j++)
               if(!strcmp(connections[j], pipename[0])) {
                  //jack_disconnect(client_, app->ports[ch][i], pipename[0]);
                  pipename[1] = jack_port_name(app->pipes[ch][i][1]);
                  //jack_disconnect(client_, pipename[1], hardware_port_names_[ch]);
                  pthread_t(tid);
                  pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[ch][i][0]);
                  pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[ch][i][1]);
                  //jack_port_unregister(client_, app->pipes[ch][i][0]);
                  //jack_port_unregister(client_, app->pipes[ch][i][1]);
                  app->pipes[ch][i][0] = NULL;
                  app->pipes[ch][i][1] = NULL;
               }
         }
      }
      jack_debug(0, "Harmonizing 4");
      return_debug(-2);
   }

   void HarmonizeApp(_app *app) {
      int i, ch;
      jack_debug(2, "Harmonizing %s (%smuted, %sstolen)", app->name, app->muted ? "" : "un",
                    app->volume ? "" : "not ");
      for(ch = 0; ch < 2; ch++)
         for(i = 0; i < app->nports[ch]; i++)
            HarmonizePort(app, ch, i);
      jack_debug_indent(-2);
   }

   void ConnectPort(_app *app, int ch, int i, int stolen, int connect) {
      exit(1);
      char port_name[100];
      jack_debug(2, "%sconnecting port: %s:%d:%d, mine: %d", connect ? "" : "un", app->name, ch, i, stolen);
      if(!stolen && !connect)
         jack_disconnect(client_, app->ports[ch][i], hardware_port_names_[ch]);
      else if(!stolen && connect)
         jack_connect(client_, app->ports[ch][i], hardware_port_names_[ch]);
      else if(stolen && !connect) {
         pthread_t tid;
         jack_disconnect(client_, app->ports[ch][i], jack_port_name(app->pipes[ch][i][0]));
         jack_disconnect(client_, jack_port_name(app->pipes[ch][i][1]), hardware_port_names_[ch]);

         jack_debug(2, "Removing pipe for %s:%d:%d; %s", app->name, ch, i, app->ports[ch][i]);
         pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[ch][i][0]);
         pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[ch][i][1]);
         jack_debug_indent(-2);
      } else if(stolen && connect) {
         jack_debug(2, "Creating pipe for %s:%d:%d; %s", app->name, ch, i, app->ports[ch][i]);
         sprintf(port_name, "input %d [%d]", next_port_num_, app->pid);
         app->pipes[ch][i][0] = jack_port_register(client_, port_name,
                JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
         sprintf(port_name, "output %d [%d]", next_port_num_, app->pid);
         app->pipes[ch][i][1] = jack_port_register(client_, port_name,
                JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
         next_port_num_++;

         jack_connect(client_, app->ports[ch][i], jack_port_name(app->pipes[ch][i][0]));
         jack_connect(client_, jack_port_name(app->pipes[ch][i][1]), hardware_port_names_[ch]);
         jack_debug(0, "Created pipe for %s:%d:%d; %s", app->name, ch, i, app->ports[ch][i]);
         jack_debug_indent(-2);
      }
      jack_debug_indent(-2);
   }

   void Mute(_app *app) {
      int ch, i;
      jack_debug(2, "%smuting %s", app->muted ? "un" : "", app->name);
      app->muted ^= 1;
      HarmonizeApp(app);
      jack_debug_indent(-2);
   }

   void KillPort(_app *app, int channel, int i) {
      int j;
      //jack_free((void *)app->ports[channel][i]);
      jack_debug(2, "killing port %s:%d:%d[0m", app->name, channel, i);
      if(app->volume) {
         pthread_t tid;
         //ConnectPort(app, channel, i, 1, 0);

         pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[channel][i][0]);
         pthread_create(&tid, NULL, __jack_unregister_in_another_thread, app->pipes[channel][i][1]);
         for(j = i + 1; j < app->nports[channel]; j++) {
            app->pipes[channel][j - 1][0] = app->pipes[channel][j][0];
            app->pipes[channel][j - 1][1] = app->pipes[channel][j][1];
         }
      }
      for(j = i + 1; j < app->nports[channel]; j++)
         app->ports[channel][j - 1] = app->ports[channel][j];
      app->nports[channel]--;
      jack_debug_indent(-2);
   }

   void SetVolume(_app *app, int volume) {
      jack_debug(2, "setting volume: %s %d", app->name, volume);
      app->volume = volume;
      app->volume_multiplier = pow(2, volume / 10.0);
      HarmonizeApp(app);
      jack_debug_indent(-2);

      // If it's not currently muted, and we're switching output mechanisms, mute and unmute.
      /*int twiddle_mute = !app->muted && ((app->volume == 0) != (volume == 0));
      jack_debug(2, "setting volume: %s %d", app->name, volume);

      if(twiddle_mute) Mute(app);

      app->volume = volume;
      app->volume_multiplier = pow(2, volume / 10.0);

      if(twiddle_mute) Mute(app);
      jack_debug_indent(-2);*/
   }

   int AppAlive(_app *app) {
      int pid;
      const char *app_name;
      //jack_debug(0, "Checking app alive: %s", app->name);
      if(app->nports[0] || app->nports[1])
         return 1;
      if(!app->pid)
         return 0;
      app_name = AppNameForJackName(app->jack_name, &pid);
      jack_debug(0, "  Checking app pid: %s", app->name);
      if(pid == app->pid && app_name && !strcmp(app_name, app->name))
         return 1;
      return 0;
   }

   _app *AppForPort(const char *port_name, int channel, int add) {
      int i, j;
      int pid;
      char *jack_name = JackNameForPort(port_name);
      if(!strcmp(jack_name, PER_APP_JACK_NAME)) {
         jack_debug(0, "Ignoring port: %s", port_name);
         free(jack_name);
         return NULL;
      }
      const char *name = AppNameForJackName(jack_name, &pid);
      for(i = 0; i < napps_; i++) {
         if(pid == apps_[i].pid && name && !strcmp(name, apps_[i].name)) {
            free(jack_name);
            free((char *)name);
            if(add && channel != -1) {
               for(j = 0; j < apps_[i].nports[channel]; j++)
                  if(!strcmp(apps_[i].ports[channel][j], port_name))
                     break;
               if(j == apps_[i].nports[channel]) {
                  apps_[i].pipes[channel][apps_[i].nports[channel]][0] = NULL;
                  apps_[i].pipes[channel][apps_[i].nports[channel]][1] = NULL;
                  apps_[i].ports[channel][apps_[i].nports[channel]++] = port_name;
               }
            }
            return &apps_[i];
         }
      }
      if(!add || napps_ == 20)
         return NULL;
      apps_[napps_].jack_name = jack_name;
      apps_[napps_].name = name;
      apps_[napps_].muted = 0;
      apps_[napps_].nports[1-channel] = 0;
      apps_[napps_].nports[channel] = 1;
      apps_[napps_].ports[channel][0] = port_name;
      apps_[napps_].pipes[channel][0][0] = NULL;
      apps_[napps_].pipes[channel][0][1] = NULL;
      apps_[napps_].pid = pid;
      apps_[napps_].volume = 0;
      return &apps_[napps_++];
   }

   const char *AppNameForJackName(const char *jack_name, int *pid) {
      int i;
      if(sscanf(jack_name, "alsa-jack.jackP.%d", pid) ||
         sscanf(jack_name, "MPlayer [%d]", pid)) {
         //jack_debug(0, "  Found pid for %s", jack_name);
         char cmdpath[100];
         char cmd[85];
         sprintf(cmdpath, "/proc/%d/cmdline", *pid);
         int cmdfd = open(cmdpath, O_RDONLY);
         if(cmdfd < 0)
            return NULL;
         int len = read(cmdfd, cmd, 80);
         close(cmdfd);
         cmd[len] = 0;
         for(i = 0; i < len - 1; i++)
            if(!cmd[i])
               cmd[i] = ' ';
         return stracpy(cmd);
      } else {
         jack_debug(0, "  Found no pid for %s", jack_name);
         *pid = 0;
         return (const char *)stracpy((char *)jack_name);
      }
   }

   char *JackNameForPort(const char *port_name) {
      int index = strchr(port_name, (int)':') - port_name;
      char *app_name = (char *)malloc(index + 1);
      strncpy(app_name, port_name, index);
      app_name[index] = 0;
      return app_name;
   }
    
} jack_ctrl;

void __jack_error(const char *desc) { jack_ctrl.ErrorHandler(desc); }
int __jack_process(jack_nframes_t nframes, void *arg) { jack_ctrl.JackProcess(nframes, arg); }
void *__jack_unregister_in_another_thread(void *port) { return jack_ctrl.UnregisterInAnotherThread(port); }
void *__jack_disconnect_in_another_thread(void *port) { return jack_ctrl.DisconnectInAnotherThread(port); }
void *__jack_harmonize_in_another_thread(void *app) { return jack_ctrl.HarmonizeInAnotherThread(app); }
void __jack_port_connected(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
   jack_ctrl.JackPortConnected(a, b, connect, arg);
}
void __jack_client_registered(const char *name, int connect, void *arg) {
   jack_ctrl.JackClientRegistered(name, connect, arg);
}
void __jack_port_registered(jack_port_id_t id, int connect, void *arg) {
   jack_ctrl.JackPortRegistered(id, connect, arg);
}

#endif /* JACK_AUDIO */

#ifdef PULSEAUDIO

#define PA_DO(cmd) do { \
      executing = 1; \
      cmd; \
      while(executing) \
         pa_mainloop_iterate(mainloop, 1, NULL); \
   } while(0)

#define PA_REF_DO(cmd) do { \
      executing = 1; \
      pa_operation_unref(cmd); \
      while(executing) \
         pa_mainloop_iterate(mainloop, 1, NULL); \
   } while(0)

#define PA_FINISHED() do { \
      ths->executing = 0;  \
      return; \
   } while(0)
   
class PulseCtrl {
   pa_context *context;
   pa_mainloop *mainloop;
   pa_mainloop_api *mainloop_api;
   int executing;
   int selected;

   int nclients;
   struct _client {
      int id;
      char name[20];
   } clients[100];

   int ninputs;
   struct {
      int id;
      struct _client *client;
      int channels;
      int volume;
      int muted;
   } inputs[100];

   static void context_state_cb(pa_context *context, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      switch(pa_context_get_state(context)) {
         case PA_CONTEXT_CONNECTING:
         case PA_CONTEXT_AUTHORIZING:
         case PA_CONTEXT_SETTING_NAME:
            break;

         case PA_CONTEXT_READY:
            ths->executing = 0;
            break;
      }
   }

   static void client_info_cb(pa_context *context,
         const pa_client_info *info, int is_last, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      if(is_last)
         PA_FINISHED();

      ths->clients[ths->nclients].id = info->index;
      strcpy(ths->clients[ths->nclients].name, info->name);
      ths->nclients++;
   }

   static void sink_input_info_cb(pa_context *context,
         const pa_sink_input_info *info, int is_last, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      if(is_last)
         PA_FINISHED();

      char cv[1000];
      int i;
      for(i = 0; i < ths->nclients; i++)
         if(info->client == ths->clients[i].id)
            break;

      ths->inputs[ths->ninputs].id = info->index;
      ths->inputs[ths->ninputs].client = &ths->clients[i];
      ths->inputs[ths->ninputs].channels = info->volume.channels;
      ths->inputs[ths->ninputs].volume = info->volume.values[0];
      ths->inputs[ths->ninputs].muted = info->mute;
      ths->ninputs++;
   }

   static void volume_cb(pa_context *context, int status, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      PA_FINISHED();
   }

   public:
   PulseCtrl() {
      selected = -1;

      mainloop = pa_mainloop_new();
      mainloop_api = pa_mainloop_get_api(mainloop);
      context = pa_context_new(mainloop_api, "osdhandler");

      pa_context_set_state_callback(context, context_state_cb, this);
      PA_DO(pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL));

      int i;
      pa_cvolume vol = {2, {65535, 65535}};
      for(i = 0; i < ninputs; i++) {
         PA_REF_DO(pa_context_set_sink_input_volume(context, inputs[i].id, &vol, volume_cb, this));
      }
   }

   ~PulseCtrl() {
      pa_context_disconnect(context);
      pa_mainloop_free(mainloop);
   }

   void SetVolume(int selected, int volume) {
      pa_cvolume vol;
      int i;

      /*if(volume > 65535)
         volume = 65535;*/
      if(volume < 0)
         volume = 0;
      vol.channels = inputs[selected].channels;
      for(i = 0; i < vol.channels; i ++)
         vol.values[i] = volume;
      PA_REF_DO(pa_context_set_sink_input_volume(context, inputs[selected].id,
               &vol, volume_cb, this));
      inputs[selected].volume = volume;
   }

   void Mute(int selected) {
      pa_cvolume vol;
      int i;

      inputs[selected].muted ^= 1;
      PA_REF_DO(pa_context_set_sink_input_mute(context, inputs[selected].id,
               inputs[selected].muted, volume_cb, this));
   }

   void Handle() {
      int i;
      char line[150];

      if(selected == -1) {
         selected = 0;
         nclients = 0;
         PA_REF_DO(pa_context_get_client_info_list(context, client_info_cb, this));
         ninputs = 0;
         PA_REF_DO(pa_context_get_sink_input_info_list(context,
                  sink_input_info_cb, this));
         if(ninputs) {
            keyboard_grabber.grab(keyboard_grabber.PULSE);
         } else {
            selected = -1;
            UL_osd.Write(_("No Pulseaudio Inputs"));
            return;
         }
      }

      int keyPressed = 0;
      if(keyboard_grabber.key_available())
         keyPressed = keyboard_grabber.get_key();
      if(keyPressed) {
         if(keyPressed == XK_Escape || keyPressed == XK_Return ||
            keyPressed == XK_c && keyboard_grabber.control_pressed == 1) {
            selected = -1;

            UL_osd.Hide();
            UL_osd.SetTimeOut(5);
            keyboard_grabber.release();
            return;
         }
         if('0' == keyPressed) SetVolume(selected, 65535);
         if('1' <= keyPressed && keyPressed <= '9')
            SetVolume(selected, (keyPressed - '0') * 65535 / 10 + 1);
         if(keyPressed == 'd')
            SetVolume(selected, inputs[selected].volume - 65535 / 50);
         if(keyPressed == 'f')
            SetVolume(selected, inputs[selected].volume + 65535 / 50);
         if(keyPressed == 'm') Mute(selected);
         if(keyPressed == 'j') selected++;
         if(keyPressed == 'k') selected--;

         char *selectors = _("qwertyuiop");
         if(strchr(selectors, keyPressed))
            selected = strchr(selectors, keyPressed) - selectors;
         selected = (selected + ninputs) % ninputs;
      }

      UL_osd.SetTimeOut(-1);
      UL_osd.Clear();
      for(i = 0; i < ninputs; i++) {
         sprintf(line, "%c %c%3d %s\n", i == selected ? '>' : ' ',
               inputs[i].muted ? 'M' : ' ',
               (inputs[i].volume * 100 + 5000) / 65535, inputs[i].client->name);
         UL_osd.WriteLine(line, i);
      }

   }


} pulse_ctrl;
#endif /* PULSEAUDIO */

#ifdef SOFTVOL
class SoftvolCtrl {

   int nclients;
   struct _client {
      int id;
      char name[20];
   };

   int ninputs;
   struct {
      int id;
      struct _client *client;
      int channels;
      int volume;
      int muted;
   } inputs[100];



   static void context_state_cb(pa_context *context, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      switch(pa_context_get_state(context)) {
         case PA_CONTEXT_CONNECTING:
         case PA_CONTEXT_AUTHORIZING:
         case PA_CONTEXT_SETTING_NAME:
            break;

         case PA_CONTEXT_READY:
            ths->executing = 0;
            break;
      }
   }

   static void client_info_cb(pa_context *context,
         const pa_client_info *info, int is_last, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      if(is_last)
         PA_FINISHED();

      ths->clients[ths->nclients].id = info->index;
      strcpy(ths->clients[ths->nclients].name, info->name);
      ths->nclients++;
   }

   static void sink_input_info_cb(pa_context *context,
         const pa_sink_input_info *info, int is_last, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      if(is_last)
         PA_FINISHED();

      char cv[1000];
      int i;
      for(i = 0; i < ths->nclients; i++)
         if(info->client == ths->clients[i].id)
            break;

      ths->inputs[ths->ninputs].id = info->index;
      ths->inputs[ths->ninputs].client = &ths->clients[i];
      ths->inputs[ths->ninputs].channels = info->volume.channels;
      ths->inputs[ths->ninputs].volume = info->volume.values[0];
      ths->inputs[ths->ninputs].muted = info->mute;
      ths->ninputs++;
   }

   static void volume_cb(pa_context *context, int status, void *userdata) {
      PulseCtrl *ths = (PulseCtrl *)userdata;

      PA_FINISHED();
   }

   public:
   PulseCtrl() {
      selected = -1;

      mainloop = pa_mainloop_new();
      mainloop_api = pa_mainloop_get_api(mainloop);
      context = pa_context_new(mainloop_api, "osdhandler");

      pa_context_set_state_callback(context, context_state_cb, this);
      PA_DO(pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL));

      int i;
      pa_cvolume vol = {2, {65535, 65535}};
      for(i = 0; i < ninputs; i++) {
         PA_REF_DO(pa_context_set_sink_input_volume(context, inputs[i].id, &vol, volume_cb, this));
      }
   }

   ~PulseCtrl() {
      pa_context_disconnect(context);
      pa_mainloop_free(mainloop);
   }

   void SetVolume(int selected, int volume) {
      pa_cvolume vol;
      int i;

      /*if(volume > 65535)
         volume = 65535;*/
      if(volume < 0)
         volume = 0;
      vol.channels = inputs[selected].channels;
      for(i = 0; i < vol.channels; i ++)
         vol.values[i] = volume;
      PA_REF_DO(pa_context_set_sink_input_volume(context, inputs[selected].id,
               &vol, volume_cb, this));
      inputs[selected].volume = volume;
   }

   void Mute(int selected) {
      pa_cvolume vol;
      int i;

      inputs[selected].muted ^= 1;
      PA_REF_DO(pa_context_set_sink_input_mute(context, inputs[selected].id,
               inputs[selected].muted, volume_cb, this));
   }

   void Handle() {
      int i;
      char line[150];

      if(selected == -1) {
         selected = 0;
         nclients = 0;
         PA_REF_DO(pa_context_get_client_info_list(context, client_info_cb, this));
         ninputs = 0;
         PA_REF_DO(pa_context_get_sink_input_info_list(context,
                  sink_input_info_cb, this));
         if(ninputs) {
            keyboard_grabber.grab(keyboard_grabber.PULSE);
         } else {
            selected = -1;
            UL_osd.Write(_("No Pulseaudio Inputs"));
            return;
         }
      }

      int keyPressed = 0;
      if(keyboard_grabber.key_available())
         keyPressed = keyboard_grabber.get_key();
      if(keyPressed) {
         if(keyPressed == XK_Escape || keyPressed == XK_Return ||
            keyPressed == XK_c && keyboard_grabber.control_pressed == 1) {
            selected = -1;

            UL_osd.Hide();
            UL_osd.SetTimeOut(5);
            keyboard_grabber.release();
            return;
         }
         if('0' == keyPressed) SetVolume(selected, 65535);
         if('1' <= keyPressed && keyPressed <= '9')
            SetVolume(selected, (keyPressed - '0') * 65535 / 10 + 1);
         if(keyPressed == 'd')
            SetVolume(selected, inputs[selected].volume - 65535 / 50);
         if(keyPressed == 'f')
            SetVolume(selected, inputs[selected].volume + 65535 / 50);
         if(keyPressed == 'm') Mute(selected);
         if(keyPressed == 'j') selected++;
         if(keyPressed == 'k') selected--;

         char *selectors = _("qwertyuiop");
         if(strchr(selectors, keyPressed))
            selected = strchr(selectors, keyPressed) - selectors;
         selected = (selected + ninputs) % ninputs;
      }

      UL_osd.SetTimeOut(-1);
      UL_osd.Clear();
      for(i = 0; i < ninputs; i++) {
         sprintf(line, "%c %c%3d %s\n", i == selected ? '>' : ' ',
               inputs[i].muted ? 'M' : ' ',
               (inputs[i].volume * 100 + 5000) / 65535, inputs[i].client->name);
         UL_osd.WriteLine(line, i);
      }

   }


} softvol_ctrl;
#endif /* SOFTVOL */

void wrapper(char **a, int b, int c) {
   executer.HandleTabs(a, b, c);
}

void executer_handler_wrapper(char *c) {
   executer.Handle(c);
}

#ifdef PULSEAUDIO
void pulse_handler_wrapper() {
   pulse_ctrl.Handle();
}
#endif /* PULSEAUDIO */

#ifdef JACK_AUDIO
void jack_handler_wrapper() {
   jack_ctrl.Handle();
}
#endif /* JACK_AUDIO */

void mouse_handler_wrapper(char *c) {
   mouse_ctrl.Handle(c);
}



void WritePid() {
   int pid;
   char *display;
   char filename[100];
   display = getenv("DISPLAY");
   if(!display)
      exit(1);
   sprintf(filename, "/home/edanaher/.fvwm/var/osd_pid%s", display);
   FILE *FIN = fopen(filename, "r");
   if(FIN) {
      fscanf(FIN, "%d", &pid);
      char fname[20];
      sprintf(fname, "/proc/%d/cmdline", pid);
      struct stat buf;
      if(!stat(fname, &buf)) {
         int delay;
         kill(pid, 15);
         for(delay = 1; delay < 1 << 15 && !stat(fname, &buf); delay <<= 1)
            usleep(delay);
#ifdef MUSICPLAYER
         music_player.LoadPlaylistState();
#endif /* MUSICPLAYER */
      }
      else
#ifdef MUSICPLAYER
         music_player.LoadPlaylistState();
#endif /* MUSICPLAYER */
      fclose(FIN);
   }
   FILE *FOUT = fopen(filename, "w");
   fprintf(FOUT, "%d\n", getpid());
   fclose(FOUT);
}

void HandlePipe(char *in) {
   int tmp, tmp2;
   char on;
   char command[1000];

   switch(in[0]) {
      case 'o':
         running = (in[2] == 't');
         break;
#ifdef VOLUME
      case ' ':
         if(sscanf(in, "  Mono: Playback %d [%d%%] [%d.%ddB] [o%c", &tmp2, &tmp, &tmp2, &tmp2, &on) != 5)
            break;
         osd_clock.SetVolume(tmp, on);
         break;
#endif /* VOLUME */
#ifdef MUSICPLAYER
      case 'm':
         sscanf(in, "m:%s", command);
         in[strlen(in)-1] = 0;
         music_player.Handle(in+2);
         break;
#endif /* MUSICPLAYER */
      case 'x':
         sscanf(in, "x:%s", command);
         executer.Handle(command);
         break;
      case 'b':
         sscanf(in, "b:%s", command);
         osd_clock.Handle(command);
         break;
#ifdef PULSEAUDIO
      case 'p':
         pulse_ctrl.Handle();
         break;
#endif /* PULSEAUDIO */
#ifdef JACK_AUDIO
      case 'p':
         jack_ctrl.Handle();
         break;
#endif /* JACK_AUDIO */
      case 'M':
         keyboard_grabber.grab(keyboard_grabber.MOUSE);
   }
   return;
}

void on_quit(int sig) {
#ifdef MUSICPLAYER
   SavePlaylistStateWrapper();
#endif /* MUSICPLAYER */
#ifdef JACK_AUDIO
   jack_ctrl.Quit();
#endif /* JACK_AUDIO */
   exit(128 + sig);
}

int main(int argc, char **argv) {
   int len;
   char in[10000], *buf;
   fflush(stdout);
   int fd_in = open("/home/edanaher/.fvwm/osd_pipe", O_RDWR);
   int fd_stdout = open("/home/edanaher/.fvwm/osd_out_pipe", O_RDWR);
   int fd_stderr = open("/home/edanaher/.fvwm/osd_err_pipe", O_RDWR);
   int i;
   struct timeval currenttime;
   int screen_width = 0, screen_height = 0;

   timeval timeout = {0L, 0L};
   fd_set read_fds;
   srandom(time(NULL) + getpid() * 0x1432435);

   WritePid();

   if(argc == 3) {
      screen_width = atoi(argv[1]);
      screen_height = atoi(argv[2]);
      NiceOsd *osd;
      for(osd = osds; osd; osd = osd->next_osd_) {
         if(screen_width)
            osd->SetScreenWidth(screen_width);
         if(screen_height)
            osd->SetScreenHeight(screen_height);
      }
         
   }
   //printf("%d %d\n", screen_width, screen_height);


   long long starttime;
   long long real_now;
   gettimeofday(&currenttime, NULL);
   starttime = currenttime.tv_sec * 1000ULL + currenttime.tv_usec/1000;
#ifdef MAILIDLER
   mail_idler.timeout_ = starttime + 5 * 60 * 1000;
#endif /* MAILIDLER */


#ifdef VOLUME
   //system("amixer -Dmixer0 set Master 0%+ | grep Mono: > ~/.fvwm/osd_pipe");
#endif /* VOLUME */

   signal(SIGTERM, on_quit);
   signal(SIGINT, on_quit);

   while(1) {
      /*if(now - starttime > 1800000)
         break; // */

      timeout.tv_sec = 0;
      timeout.tv_usec = (1000 - (real_now % 1000) + 1) * 1000;
      FD_ZERO(&read_fds);
      FD_SET(fd_in, &read_fds);
      FD_SET(keyboard_grabber.fd, &read_fds);
#ifdef MAILIDLER
      if(mail_idler.status_ != mail_idler.ERROR)
         FD_SET(mail_idler.fd, &read_fds);
#endif /* MAILIDLER */
      FD_SET(fd_stdout, &read_fds);
      FD_SET(fd_stderr, &read_fds);
#ifdef MAILIDLER
      int max_fd = max(max4(fd_in, fd_stdout, fd_stderr, keyboard_grabber.fd),
                       mail_idler.fd);
#else /* MAILIDLER */
      int max_fd = max4(fd_in, fd_stdout, fd_stderr, keyboard_grabber.fd);
#endif /* MAILIDLER */
      if(select(max_fd+1, &read_fds, NULL, NULL, &timeout) > 0) {
         if(FD_ISSET(fd_in, &read_fds)) {
            len = read(fd_in, in, 10000);
            in[len] = 0;
            HandlePipe(in);
         }
         if(FD_ISSET(fd_stdout, &read_fds)) {
            len = read(fd_stdout, in, 10000);
            in[len] = 0;
            executer.Output(in);
         }
         if(FD_ISSET(fd_stderr, &read_fds)) {
            len = read(fd_stderr, in, 10000);
            in[len] = 0;
            executer.Error(in);
         }
         if(FD_ISSET(keyboard_grabber.fd, &read_fds)) {
            keyboard_grabber.Handle(NULL);
         }
#ifdef MAILIDLER
         if(FD_ISSET(mail_idler.fd, &read_fds)) {
            mail_idler.Handle(1);
            printf("Mail idler fd\n");
         }
#endif /* MAILIDLER */
      }

      gettimeofday(&currenttime, NULL);
      real_now = currenttime.tv_sec * 1000ULL + currenttime.tv_usec/1000;
      now = real_now - real_now % 1000;

#ifdef MAILIDLER
      if(mail_idler.timeout_ < now) {
         printf("Mail idler timeout\n");
         mail_idler.Handle(0);
      }
#endif /* MAILIDLER */

      if(mouse_ctrl.active)
         mouse_ctrl.run();
#ifdef MUSICPLAYER
      TIME(music_player, music_player.Update(running));
#endif /* MUSICPLAYER */
      if(running) {
         osd_clock.Update();
      }
      else {
         osd_clock.Hide();
      }
      //if(now - starttime > 10 && !((now - starttime) / 1000 % 30)) printCounters();
   }
   return 0;
}
