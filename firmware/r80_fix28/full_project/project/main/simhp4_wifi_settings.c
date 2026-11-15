/* simhP4 R6R56 - Full-screen 1280x720 touch Wi-Fi settings.
 * Only existing host presenter paints/touches the screen; no guest changes.
 * Every painted control shares its hit-test rectangle (no hidden targets).
 */
#include "simhp4_wifi_settings.h"
/* Read only a credential identity, never a password. No calls from an ISR. */
extern int simhp4_wifi_backend_saved_ssid_copy(char *dest, size_t capacity);
extern void simhp4_bsd_net_init_request(void);
/* SIMHP4_R80_REV13_ROOT_ACCESS: enable temporary root on one BSD pty. */
extern void simhp4_bsd_remote_access_request(void);
extern void simhp4_bsd_fast_setup_request(void);
extern void simhp4_r19_suspend_request(void);
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void cp(char *d,size_t z,const char *s){if(!z)return;if(!s)s="";size_t n=strlen(s);if(n>=z)n=z-1;memcpy(d,s,n);d[n]=0;}
static void wipe(simhp4_wifi_ui*u){volatile char*p=u->password;for(size_t i=0;i<sizeof u->password;++i)p[i]=0;u->password_len=0;}
static int eq(const char*a,const char*b){return strcmp(a,b)==0;}
static int hit(int px,int py,int x,int y,int w,int h){return px>=x&&py>=y&&px<x+w&&py<y+h;}
static int max_i(int a,int b){return a>b?a:b;}

typedef struct {int m,g,header,panel_y,panel_h,left_w,right_x,right_w;
 int list_top,row_h,visible,tool_y,tool_h,pw_y,pw_h,key_y,key_h,key_gap;
} geometry;
static geometry geom(const simhp4_wifi_ui*u){
 geometry z={0};
 z.m=max_i(8,u->width/100);z.g=max_i(7,u->width/115);
 z.header=max_i(46,u->height*8/100);
 z.panel_y=z.header+5;
 z.panel_h=u->height*34/100;
 z.left_w=(u->width-2*z.m-z.g)*61/100;
 z.right_x=z.m+z.left_w+z.g;
 z.right_w=u->width-z.right_x-z.m;
 z.list_top=z.panel_y+46;
 z.row_h=max_i(37,(z.panel_h-49)/5);
 z.visible=5;
 z.tool_y=z.panel_y+z.panel_h+7;
 z.tool_h=max_i(51,u->height*8/100);
 z.pw_y=z.tool_y+z.tool_h+7;
 z.pw_h=max_i(64,u->height*10/100);
 z.key_y=z.pw_y+z.pw_h+7;
 z.key_gap=6;
 z.key_h=(u->height-z.key_y-z.m-4*z.key_gap)/5;
 if(z.key_h<30)z.key_h=30;
 return z;
}
static simhp4_wifi_badge_rect badge(const simhp4_wifi_ui*u){
 simhp4_wifi_badge_rect b={u->width-194,9,176,58};return b;
}
/* SIMHP4_R80_FIX11: adjacent to the unchanged 176x58 Wi-Fi badge. */
static simhp4_wifi_badge_rect bsd_macro_button(const simhp4_wifi_ui*u){
 simhp4_wifi_badge_rect b={u->width-382,9,170,58};return b;
}
static simhp4_wifi_badge_rect remote_access_button(const simhp4_wifi_ui*u){
 simhp4_wifi_badge_rect b={u->width-566,9,165,58}; return b;
}
static simhp4_wifi_badge_rect r19_susp_button(const simhp4_wifi_ui*u){
 simhp4_wifi_badge_rect b={u->width-952,9,184,58};return b;
}
static simhp4_wifi_badge_rect r80_fast_button(const simhp4_wifi_ui*u){
 simhp4_wifi_badge_rect b={u->width-760,9,184,58}; return b;
}
static int rows_on_page(const simhp4_wifi_ui*u){(void)u;return 5;}
static void choose(simhp4_wifi_ui*u,int i){
 if(i<0||i>=u->ap_count)return;
 u->selected=i;cp(u->ssid,sizeof(u->ssid),u->aps[i].ssid);wipe(u);
 u->uppercase=0;u->show_password=0;u->revision++;
}
static void submit(simhp4_wifi_ui*u){
 if(!u->ssid[0])return;
 if(u->ops.request_connect)u->ops.request_connect(u->ops.opaque,u->ssid,u->password);
 wipe(u);u->revision++;
}
static void append(simhp4_wifi_ui*u,char ch){
 if(u->selected<0 || !u->aps[u->selected].secure)return;
 if(u->password_len>=sizeof(u->password)-1)return;
 u->password[u->password_len++]=ch;u->password[u->password_len]=0;u->revision++;
}
void simhp4_wifi_ui_init(simhp4_wifi_ui*u,int w,int h,simhp4_wifi_ui_ops ops){
 if(!u)return;
 memset(u,0,sizeof(*u));u->width=w;u->height=h;u->ops=ops;u->selected=-1;}
void simhp4_wifi_ui_set_link(simhp4_wifi_ui*u,simhp4_wifi_link_state state,const char*ssid,const char*ip){
 if(!u)return;
 if(!ssid)ssid="";
 if(!ip)ip="";
 if(u->state==state&&eq(u->connected_ssid,ssid)&&eq(u->address,ip))return;
 u->state=state;cp(u->connected_ssid,sizeof u->connected_ssid,ssid);
 cp(u->address,sizeof u->address,ip);u->revision++;
}
void simhp4_wifi_ui_set_radio_status(simhp4_wifi_ui*u,const char*status){
 if(!u)return;
 if(!status)status="";
 if(eq(u->radio_status,status))return;
 cp(u->radio_status,sizeof u->radio_status,status);u->revision++;
}
void simhp4_wifi_ui_set_aps(simhp4_wifi_ui*u,const simhp4_wifi_ap*a,int n){
 if(!u)return;
 if(n<0)n=0;
 if(n>SIMHP4_WIFI_MAX_APS)n=SIMHP4_WIFI_MAX_APS;
 if(n&&!a)return;
 /* Presenter snapshots may repeat unchanged scan results at high frequency.
  * Do not reset pages or request a full-screen repaint for identical APs. */
 int changed=(u->ap_count!=n);
 if(!changed){
  for(int i=0;i<n;i++){
   if(!eq(u->aps[i].ssid,a[i].ssid)||u->aps[i].rssi!=a[i].rssi||
      u->aps[i].secure!=a[i].secure){changed=1;break;}
  }
 }
 if(!changed)return;
 /* Keep the user's page while a scan refreshes the list. */
 int previous_page=u->page;
 /* Keep selection by SSID across rescan; never silently join a different AP. */
 char previous[SIMHP4_WIFI_SSID_SIZE];cp(previous,sizeof previous,u->ssid);
 u->ap_count=n;u->selected=-1;
 for(int i=0;i<n;i++){
  cp(u->aps[i].ssid,sizeof u->aps[i].ssid,a[i].ssid);
  u->aps[i].rssi=a[i].rssi;u->aps[i].secure=a[i].secure;
  if(previous[0]&&eq(u->aps[i].ssid,previous))u->selected=i;
 }
 if(u->selected<0){u->ssid[0]=0;wipe(u);}
 int max_page=n>0?(n-1)/rows_on_page(u):0;
 u->page=previous_page<=max_page?previous_page:max_page;
 u->revision++;
}
int simhp4_wifi_ui_is_open(const simhp4_wifi_ui*u){return u?u->open:0;}

/* Keyboard rows; each has a painted/interactive BS, SHIFT, etc. */
static const char *const row_keys[4]={"1234567890-@","qwertyuiop._","asdfghjkl:/","zxcvbnm,!?_"};
static int keyboard_hit(simhp4_wifi_ui*u,const geometry*z,int x,int y){
 for(int r=0;r<4;r++){
  int yy=z->key_y+r*(z->key_h+z->key_gap);
  int x0=z->m,available=u->width-2*z->m;
  int special=(r==0||r==2)?1:0;
  const char*keys=row_keys[r];int n=(int)strlen(keys);
  int segments=n+special;
  int colw=available/segments;
  for(int k=0;k<segments;k++){
   int xx=x0+k*colw;
   int ww=(k==segments-1?available-k*colw:colw-z->key_gap);
   if(!hit(x,y,xx,yy,ww,z->key_h))continue;
   if(special&&k==n){
    if(r==0){if(u->password_len){u->password[--u->password_len]=0;u->revision++;}}
    else{u->uppercase=!u->uppercase;u->revision++;}
   }else{
    char ch=keys[k];if(u->uppercase)ch=(char)toupper((unsigned char)ch);
    append(u,ch);
   }
   return 1;
  }
 }
 return 0;
}
int simhp4_wifi_ui_touch(simhp4_wifi_ui*u,int x,int y){
 if(!u)return 0;
 if(!u->open){simhp4_wifi_badge_rect b=badge(u);
  simhp4_wifi_badge_rect cmd=bsd_macro_button(u);
  simhp4_wifi_badge_rect ra=remote_access_button(u);
  simhp4_wifi_badge_rect fast=r80_fast_button(u);
  simhp4_wifi_badge_rect susp=r19_susp_button(u);
  if(simhp4_wifi_badge_hit(&susp,x,y)) {
   simhp4_r19_suspend_request();u->revision++;return 1;
  }
  if(simhp4_wifi_badge_hit(&fast,x,y)) {
   simhp4_bsd_fast_setup_request();
   u->revision++;
   return 1;
  }
  if(simhp4_wifi_badge_hit(&ra,x,y)) {
   simhp4_bsd_remote_access_request();
   u->revision++;
   return 1;
  }
  if(simhp4_wifi_badge_hit(&cmd,x,y)) {
   simhp4_bsd_net_init_request();
   u->revision++;
   return 1;
  }
  if(!simhp4_wifi_badge_hit(&b,x,y))return 0;
  u->open=1;u->revision++;
  if(u->ops.request_scan)u->ops.request_scan(u->ops.opaque);
  return 1;
 }
 /* Full screen belongs to the host while open: no guest TTY key leakage. */
 geometry z=geom(u);
 /* Upper-right X and bottom toolbar BACK are both easily touched. */
 int bw=(u->width-2*z.m-4*z.g)/5;
 if(hit(x,y,u->width-z.m-80,8,80,z.header-12) ||
    hit(x,y,z.m+4*(bw+z.g),z.tool_y,bw,z.tool_h)){
  u->open=0;wipe(u);u->revision++;return 1;
 }
 /* Five large list rows and separate page up/down hit zones. */
 if(hit(x,y,z.m+10,z.list_top,z.left_w-118,z.row_h*z.visible)){
  int slot=(y-z.list_top)/z.row_h;
  int idx=u->page*z.visible+slot;
  if(idx>=0&&idx<u->ap_count)choose(u,idx);
  return 1;
 }
 if(hit(x,y,z.m+z.left_w-92,z.list_top,80,z.row_h*2)){
  if(u->page>0){u->page--;u->revision++;}return 1;
 }
 if(hit(x,y,z.m+z.left_w-92,z.list_top+z.row_h*3,80,z.row_h*2)){
  if((u->page+1)*z.visible<u->ap_count){u->page++;u->revision++;}return 1;
 }
 /* Action buttons: all neutral outlines, including CONNECT. */
 for(int k=0;k<4;k++){
  int bx=z.m+k*(bw+z.g);
  if(!hit(x,y,bx,z.tool_y,bw,z.tool_h))continue;
  if(k==0){u->revision++;if(u->ops.request_scan)u->ops.request_scan(u->ops.opaque);}
  if(k==1)submit(u);
  if(k==2){u->revision++;if(u->ops.request_disconnect)u->ops.request_disconnect(u->ops.opaque);}
  if(k==3){u->revision++;if(u->ops.request_forget)u->ops.request_forget(u->ops.opaque);}
  return 1;
 }
 /* Show/hide PW is an actual tappable button, not decorative checkbox. */
 if(hit(x,y,u->width-z.m-172,z.pw_y+10,165,z.pw_h-18)){
  u->show_password=!u->show_password;u->revision++;return 1;
 }
 if(keyboard_hit(u,&z,x,y))return 1;
 int yy=z.key_y+4*(z.key_h+z.key_gap),all=u->width-2*z.m;
 int spacew=all*58/100, cancelw=all*20/100, okx=z.m+spacew+z.g;
 if(hit(x,y,z.m,yy,spacew,z.key_h)){append(u,' ');return 1;}
 if(hit(x,y,okx,yy,cancelw-z.g,z.key_h)){wipe(u);u->revision++;return 1;}
 if(hit(x,y,okx+cancelw,yy,all-(okx+cancelw-z.m),z.key_h)){
  submit(u);return 1;
 }
 return 1;
}

static void rect(const simhp4_wifi_badge_painter*p,int x,int y,int w,int h,uint16_t c){if(w>0&&h>0)p->fill_rect(p->context,x,y,w,h,c);}
static void text(const simhp4_wifi_badge_painter*p,const char*s,int x,int y,uint16_t c){p->draw_text(p->context,s,x,y,c);}
static void ln(const simhp4_wifi_badge_painter*p,int x,int y,int x2,int y2,uint16_t c){p->draw_line(p->context,x,y,x2,y2,c);}
static void frame(const simhp4_wifi_badge_painter*p,int x,int y,int w,int h,uint16_t c){
 ln(p,x,y,x+w-1,y,c);ln(p,x,y+h-1,x+w-1,y+h-1,c);
 ln(p,x,y,x,y+h-1,c);ln(p,x+w-1,y,x+w-1,y+h-1,c);
}
static void button(const simhp4_wifi_badge_painter*p,int x,int y,int w,int h,
                   const char*s,uint16_t bg,uint16_t fg){
 rect(p,x,y,w,h,bg);frame(p,x,y,w,h,fg);
 /* Text defaults to M5GFX base font, amplified by host draw_text callback. */
 text(p,s,x+10,y+max_i(7,(h-35)/2),fg);
}
static const char*state_name(simhp4_wifi_link_state s){
 return s==SIMHP4_WIFI_IP_READY?"CONNECTED":s==SIMHP4_WIFI_CONNECTING?"CONNECTING":"DISCONNECTED";
}
static void signal(const simhp4_wifi_badge_painter*p,int x,int y,int rssi,uint16_t fg,uint16_t dim){
 int n=rssi>=-55?4:rssi>=-68?3:rssi>=-80?2:1;
 for(int k=0;k<4;k++)rect(p,x+k*12,y+24-(k+1)*6,9,(k+1)*6,k<n?fg:dim);
}
void simhp4_wifi_ui_draw(simhp4_wifi_ui*u,const simhp4_wifi_ui_theme*t,const simhp4_wifi_badge_painter*p){
 if(!u||!t||!p||!p->draw_line||!p->draw_text||!p->fill_rect)return;
 if(!u->open){
  simhp4_wifi_badge_rect b=badge(u),cmd=bsd_macro_button(u);
  simhp4_wifi_badge_rect ra=remote_access_button(u),fast=r80_fast_button(u);
  simhp4_wifi_badge_draw(&b,&t->colors,p,u->state);
  simhp4_wifi_badge_rect susp=r19_susp_button(u);
  button(p,susp.x,susp.y,susp.width,susp.height,"SUSPEND",t->colors.black,t->colors.white);
  button(p,fast.x,fast.y,fast.width,fast.height,"FAST SETUP",t->colors.black,t->colors.white);
  button(p,ra.x,ra.y,ra.width,ra.height,"ROOT ACCESS",t->colors.black,t->colors.white);
  button(p,cmd.x,cmd.y,cmd.width,cmd.height,"BSD INIT",t->colors.black,t->colors.white);
  return;
 }
 geometry z=geom(u);
 /* Original simhP4 terminal geometry, without accent colors or neon styling.
  * Every RGB565 value below is neutral gray: R=G=B (approximately).
  * White-on-black controls; selected AP remains black-on-white. */
 const uint16_t white=0xffff, black=0x0000;
 const uint16_t ink=0xef7d, muted=0xbdf7, dim=0x630c, pale=0x8410;
 const uint16_t bg=black, fill=white, selected_ink=black;
 /* True native fullscreen (uses M5.Display.width/height, including 180deg rotation). */
 rect(p,0,0,u->width,u->height,bg);
 frame(p,z.m,3,u->width-2*z.m,z.header-6,muted);
 text(p,"simhP4  Wi-Fi SETTINGS",z.m+13,12,white);
 char status[96];snprintf(status,sizeof(status),"Wi-Fi: %s",u->state==SIMHP4_WIFI_IP_READY?"ON":"OFF");
 text(p,status,u->width-z.m-340,14,white);
 button(p,u->width-z.m-80,8,80,z.header-12,"X",bg,white);

 rect(p,z.m,z.panel_y,z.left_w,z.panel_h,bg);
 frame(p,z.m,z.panel_y,z.left_w,z.panel_h,muted);
 char head[70];snprintf(head,sizeof head,"NETWORKS (%d)",u->ap_count);
 text(p,head,z.m+13,z.panel_y+10,white);
 text(p,"SIGNAL",z.m+z.left_w-238,z.panel_y+10,muted);
 /* A single saved SSID is supported by existing simhp4wifi NVS schema.
  * Do NOT expose the password or imply other APs have saved keys.
  */
 char saved_ssid[SIMHP4_WIFI_SSID_SIZE]={0};
 (void)simhp4_wifi_backend_saved_ssid_copy(saved_ssid,sizeof saved_ssid);
 const int rowx=z.m+10,roww=z.left_w-118;
 for(int k=0;k<z.visible;k++){
  int idx=u->page*z.visible+k;
  int yy=z.list_top+k*z.row_h;
  if(idx>=u->ap_count){ln(p,rowx,yy+z.row_h-1,rowx+roww-1,yy+z.row_h-1,dim);continue;}
  const int selected=idx==u->selected;
  rect(p,rowx,yy,roww,z.row_h-2,selected?fill:bg);
  if(selected)frame(p,rowx,yy,roww,z.row_h-2,white);
  char label[64];
  const int saved=saved_ssid[0]&&strcmp(saved_ssid,u->aps[idx].ssid)==0;
  snprintf(label,sizeof label,"%c %d %s %.22s",selected?'>':' ',idx+1,
           saved?"[S]":"   ",u->aps[idx].ssid);
  text(p,label,rowx+12,yy+6,selected?selected_ink:ink);
  signal(p,rowx+roww-80,yy+7,u->aps[idx].rssi,selected?selected_ink:ink,selected?pale:dim);
  text(p,u->aps[idx].secure?"*":" ",rowx+roww-24,yy+5,selected?selected_ink:ink);
 }
 button(p,z.m+z.left_w-92,z.list_top,80,z.row_h*2,"UP",bg,white);
 button(p,z.m+z.left_w-92,z.list_top+z.row_h*3,80,z.row_h*2,"DOWN",bg,white);

 frame(p,z.right_x,z.panel_y,z.right_w,z.panel_h,muted);
 text(p,"SELECTED NETWORK",z.right_x+12,z.panel_y+10,white);
 ln(p,z.right_x+10,z.panel_y+43,z.right_x+z.right_w-10,z.panel_y+43,dim);
 const simhp4_wifi_ap*ap=(u->selected>=0&&u->selected<u->ap_count)?&u->aps[u->selected]:NULL;
 const int saved_selected=ap&&saved_ssid[0]&&strcmp(ap->ssid,saved_ssid)==0;
 char l[100];snprintf(l,sizeof l,"SSID: %.26s",ap?ap->ssid:"(SELECT NETWORK)");
 text(p,l,z.right_x+12,z.panel_y+51,white);
 snprintf(l,sizeof l,"SECURITY: %s%s",ap?(ap->secure?"WPA":"OPEN"):"-",
          saved_selected?" [SAVED]":"");
 text(p,l,z.right_x+12,z.panel_y+88,white);
 snprintf(l,sizeof l,"STATUS: %s",state_name(u->state));
 text(p,l,z.right_x+12,z.panel_y+125,white);
 snprintf(l,sizeof l,"IP: %.19s",u->address[0]?u->address:"-");
 text(p,l,z.right_x+12,z.panel_y+162,white);
 snprintf(l,sizeof l,"C6: %.34s",u->radio_status[0]?u->radio_status:"-");
 text(p,l,z.right_x+12,z.panel_y+199,muted);

 const char*controls[5]={"RE-SCAN",saved_selected&&!u->password_len?"CONNECT SAVED":"CONNECT",
  "DISCONNECT","FORGET","BACK"};
 const uint16_t colors[5]={white,white,dim,dim,white};
 int bw=(u->width-2*z.m-4*z.g)/5;
 for(int k=0;k<5;k++)button(p,z.m+k*(bw+z.g),z.tool_y,bw,z.tool_h,
 controls[k],bg,colors[k]);
 frame(p,z.m,z.pw_y,u->width-2*z.m,z.pw_h,muted);
 const char*selected_name=ap?ap->ssid:"SELECT NETWORK ABOVE";
 char pwtitle[80];snprintf(pwtitle,sizeof pwtitle,"PASSWORD - %.26s",selected_name);
 text(p,pwtitle,z.m+12,z.pw_y+6,white);
 char password[SIMHP4_WIFI_PASS_SIZE+4];
 size_t len=u->password_len;
 if(len>SIMHP4_WIFI_PASS_SIZE-1)len=SIMHP4_WIFI_PASS_SIZE-1;
 for(size_t i=0;i<len;i++)password[i]=u->show_password?u->password[i]:'*';
 password[len]=0;
 /* Avoid exposing any password in UART logs. */
 text(p,password,z.m+16,z.pw_y+35,white);
 button(p,u->width-z.m-172,z.pw_y+10,165,z.pw_h-18,
 u->show_password?"HIDE":"SHOW",bg,white);

 for(int r=0;r<4;r++){
  const char*keys=row_keys[r];int n=(int)strlen(keys);
  int special=(r==0||r==2);int segment=n+special;
  int all=u->width-2*z.m,colw=all/segment;
  int yy=z.key_y+r*(z.key_h+z.key_gap);
  for(int k=0;k<segment;k++){
   char buf[24]={0};
   if(special&&k==n){cp(buf,sizeof buf,r==0?"BS":"SHIFT");}
   else{buf[0]=u->uppercase?(char)toupper((unsigned char)keys[k]):keys[k];buf[1]=0;}
   int xx=z.m+k*colw;
   int ww=k==segment-1?all-k*colw:colw-z.key_gap;
   button(p,xx,yy,ww,z.key_h,buf,bg,(special&&k==n)?ink:white);
  }
 }
 int yy=z.key_y+4*(z.key_h+z.key_gap),all=u->width-2*z.m;
 int spacew=all*58/100,cancelw=all*20/100,okx=z.m+spacew+z.g;
 button(p,z.m,yy,spacew,z.key_h,"SPACE",bg,white);
 button(p,okx,yy,cancelw-z.g,z.key_h,"CANCEL",bg,white);
 button(p,okx+cancelw,yy,all-(okx+cancelw-z.m),z.key_h,"OK / CONNECT",bg,white);
}
