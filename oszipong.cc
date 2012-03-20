/*
   ein pong-spiel fuer das grandiose udssr-oszilloskop aus chemnitz
   gebastelt auf den chemnitzer linuxtagen 17./18.03.2012
   autor: johannes <jkroll bei lavabit punkt com>
   dieses programm ist gemeinfrei (public domain).
   die idee zum benutzen des oszi als display stammt von schaf, seinen
   coolen oszi-code findet man hier: http://s.drschaf.de/foo/
   
   die schlaeger werden gesteuert mit den tasten q/a und o/l
   wenn eine weile keine tasten gedrueckt werden, bewegen sich die
   schlaeger selbsttaetig (gegen computer spielen / 'demomodus')
   
   das oszi wird im x/y-modus an die soundkarte angeschlossen und so
   als bildschirm zweckentfremdet.
   der einfachheit halber wird zur audioausgabe OSS benutzt,
   je nach distro muss ggf. das kernelmodul zur alsa-oss-emulation
   installiert werden (snd_pcm_oss o.ae.)
   
   viel spass :o)
*/

#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <poll.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/soundcard.h>

static unsigned long audioFramesWritten;
static unsigned long audioFramesDelta;
static unsigned long samplingRate= 44100;
static int ossHandle;

int ossOpen(const char *dev= "/dev/dsp")
{
    int fd= open(dev, O_WRONLY);
    if(fd<0) { perror("open"); return -1; }
    int channels= 2;
    if(ioctl(fd, SNDCTL_DSP_CHANNELS, &channels)<0 || channels!=2)
    { perror("ioctl/channels"); close(fd); return -1; }
    int format= AFMT_S16_LE;
    if(ioctl(fd, SNDCTL_DSP_SETFMT, &format)<0 || format!=AFMT_S16_LE)
    { perror("ioctl/fmt"); close(fd); return -1; }
    int rate= 44100;
    if(ioctl(fd, SNDCTL_DSP_SPEED, &rate)<0)
    { perror("ioctl/rate"); close(fd); return -1; }
    samplingRate= rate;
    return fd;
}

double ossGetOutputDelay()
{
    int delay;
    ioctl(ossHandle, SNDCTL_DSP_GETODELAY, &delay);
    return double(delay)/(samplingRate*2*2);
}

// die zeit wird ueber die audioausgabe definiert, das ist ziemlich genau und fein "granuliert"
double getTime(void)
{
    return double(audioFramesWritten)/double(samplingRate);
}

double timeDelta(void)
{
    return double(audioFramesDelta)/double(samplingRate);
}

void writeCoord(int p)
{
    if(p<0) p= 0;           // kann eigentlich weg
    if(p>65535) p= 65535;   //
    p-= 32767;  // signed 16-bit samplingformat (wird von den meisten soundkarten unterstuetzt)
    uint16_t v= p;
    while(ossGetOutputDelay()>0.075) 
        usleep(10000);  // die alsa-oss-emulation erzeugt riesige puffer, die sekundenlange delays zwischen keyboard-eingaben
                        // und anzeige erzeugen. hier wird etwas gewartet, damit der puffer nicht zu groﬂ wird.
    write(ossHandle, &v, 2);
}

// pixel setzen, indem der kathodenstrahl an die entsprechende position geschickt wird.
void writeXY(int x, int y, int n)
{
    if(x<0||x>65535 || y<0||y>65535) return;
    for(int i= 0; i<n; i++)
        writeCoord(y),
        writeCoord(65530-x),
        audioFramesWritten++;
}

// ausgef¸lltes rechteck malen. funktioniert nicht sehr gut, dauert zu lang.
void drawRect(int x_, int y_, int w, int h, int n, int step)
{
    for(int x= 0; x<w; x+= step)
        for(int y= 0; y<h; y+= step)
            writeXY(x+x_, y+y_, n);
}

// 2 vertikale linien nebeneinander zeichnen, abwechselnd ein pixel links/rechts. 
// bei soundkarten mit funktionierendem tiefpass am ausgang entsteht dann ein gefuelltes rechteck.
void drawFrame(int x_, int y_, int w, int h, int n)
{
    for(int y= 0; y<h; y+= 256)
        writeXY(  x_, y+y_, n),
        writeXY(x_+w, y+y_, n);
}

// kreis zeichnen.
void drawCircle(int x, int y, int r, int n)
{
    int u= int(2*r*M_PI/256);
    for(int i= 0; i<u*2; i+= 10)
        writeXY(x + cos(i*M_PI*2/u)*r, y + sin(i*M_PI*2/u)*r, n);
}

// basisklasse fuer dinger die sich bewegen.
struct movingThing
{
    int x, y, w, h, velX, velY;
    
    movingThing(): x(0), y(0), w(65536/32), h(65536/32), velX(0), velY(0)
    { }
    
    void advance()
    {
        x+= velX*timeDelta()*100;
        y+= velY*timeDelta()*100;
    }
};

// der schlaeger.
struct paddle: movingThing
{
    double lastInput;
    
    paddle() 
    {
        w= 65536/16; 
        h= 65536/3;
    }

    void vertMoveTick()
    {
        advance();
        if(h+y>65535) 
            y= 65535-h,
            velY= -velY;
        if(y<0)
            y= 0,
            velY= -velY;
    }
    
    void playerTick()
    {
        advance();
        if(h+y>65535) 
            y= 65535-h;
        if(y<0)
            y= 0;
    }

    void ballTick(movingThing& b)
    {
        int maxVel= 256;
        if((y+h/2) - (b.y+b.h/2) > 0)
            velY= -maxVel;
        else
            velY= maxVel;
        advance();
        if(y+h>65535) y= 65535-h;
        if(y<0) y= 0;
    }
};

// klasse fuer den ball.
struct ball: movingThing
{
    ball()
    {
        x= 32768-w/2;
        y= 32768-h/2;
    }
    
    void tick()
    {
        advance();
        if(h+y>65535) 
            y= 65535-h,
            velY= -velY;
        if(y<0)
            y= 0,
            velY= -velY;
    }
    
    bool collides(int x1, int y1, int x2, int y2)
    {
        return x+w>=x1 && x<=x2 && 
               y>=y1 && y<=y2;
    }
};

// das spiel an sich (tm)
struct tehGame
{
    paddle paddleA, paddleB;
    ::ball ball;
    bool quit;
    movingThing boing;
    unsigned long lastAudioFrames;
    
    void reset()
    {
        paddleA.velY= 512;
        paddleB.x= 65535-paddleB.w;
        paddleB.y= 65535-paddleB.h;
        paddleB.velY= -512;
        ball.x= 32768-ball.w/2;
        ball.y= 32768-ball.h/2;
        ball.velX= (rand()%3+1)*(rand()&1? 200: -200);     //rand()%1024-512;
        ball.velY= (rand()%3+1)*(rand()&1? 200: -200);     //rand()%1024-512;
    }
    
    tehGame(): quit(false), lastAudioFrames(0)
    {
        reset();
        boing.x= boing.y= 32768;
    }
    
    // tastatureingaben verarbeiten. 
    // nur tastendruecke koennen benutzt werden, loslassen der tasten erzeugt kein ereignis.
    // fuer besseres input handling waere es vernuenftig, sdl zu verwenden.
    void pollInput()
    {
        pollfd ev= { STDIN_FILENO, POLLIN, 0 };
        int p= poll(&ev, 1, 0);
        if(p<0) perror("poll");
        if(p>0)
        {
            if(ev.revents&(POLLERR|POLLRDHUP|POLLHUP|POLLNVAL))
                perror("poll"),
                quit= true;
            else if(ev.revents&POLLIN)
            {
                char c;
                int foo= fread(&c, 1, 1, stdin);
                int vel= 512;
                fprintf(stderr, "%c\n", c);
                switch(c)
                {
                    case 27:   // escape
                        quit= true;
                        break;
                    case 'q':
                        paddleA.velY= -vel;
                        paddleA.lastInput= getTime();
                        break;
                    case 'a':
                        paddleA.velY= vel;
                        paddleA.lastInput= getTime();
                        break;
                    case 'o':
                        paddleB.velY= -vel;
                        paddleB.lastInput= getTime();
                        break;
                    case 'l':
                        paddleB.velY= vel;
                        paddleB.lastInput= getTime();
                        break;
                }
            }
        }
    }
    
    void tick()
    {
        audioFramesDelta= audioFramesWritten-lastAudioFrames;
        lastAudioFrames= audioFramesWritten;
        double t= getTime();
        double timeout= 15.0;
        if(t-paddleA.lastInput>timeout)
        { if(ball.velX<0) paddleA.ballTick(ball); } 
        else paddleA.playerTick();
        if(t-paddleB.lastInput>timeout)
        { if(ball.velX>0) paddleB.ballTick(ball); }
        else paddleB.playerTick();
        ball.tick();
        if(ball.collides(paddleA.x, paddleA.y, paddleA.x+paddleA.w, paddleA.y+paddleA.h))
            ball.velX= -ball.velX,
            ball.x= paddleA.w,
            boing.x= ball.x, boing.y= ball.y, boing.w= 65536/16, boing.velX= 50000;
        if(ball.collides(paddleB.x, paddleB.y, paddleB.x+paddleB.w, paddleB.y+paddleB.h))
            ball.velX= -ball.velX,
            ball.x= 65535-paddleB.w-ball.w,
            boing.x= ball.x, boing.y= ball.y, boing.w= 65536/16, boing.velX= 50000;
        if(ball.x<0)
            boing.x= ball.x, boing.y= ball.y, boing.w= 65536/16, boing.velX= 10000,
            reset();
        if(ball.x+ball.w>65535)
            boing.x= ball.x, boing.y= ball.y, boing.w= 65536/16, boing.velX= 10000,
            reset();
        drawFrame(paddleA.x,paddleA.y, paddleA.w,paddleA.h, 2);
        drawFrame(paddleB.x,paddleB.y, paddleB.w,paddleB.h, 2);
        drawFrame(ball.x,ball.y, ball.w,ball.h, 3);
        
        // der ball sieht als rechteck irgendwie "originaler" aus. :)
        //drawCircle(ball.x+ball.w/2,ball.y+ball.w/2, ball.w/2, 1);
        
        drawFrame(0,0, 65535,500, 1);
        drawFrame(0,65535-500, 65535,500, 1);
        
        if(boing.w)
        {
            drawCircle(boing.x,boing.y, boing.w, 1);
            boing.w+= timeDelta()*boing.velX;
            if(boing.w>32768) boing.w= 0;
        }
        
        pollInput();
    }
};

void setUnbufferedStdin(bool restore= false)
{
    static termios oldSettings;
    termios newSettings;

    if(!restore)
    {
        tcgetattr(STDIN_FILENO, &oldSettings);
        newSettings= oldSettings;
        newSettings.c_lflag&= ~(ICANON|ECHO);  // disable canonical (line-oriented) mode and echoing
        newSettings.c_cc[VTIME]= 1;     // timeout (tenths of seconds)
        newSettings.c_cc[VMIN]= 0;      // minimum number of characters to buffer
        //cfmakeraw(&newSettings);
        tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);
    }
    else
        tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
}

void atexitfn(void)
{
    setUnbufferedStdin(true);
}

int main(int argc, char *argv[]) 
{
    tehGame game;
    setUnbufferedStdin();
    atexit(atexitfn);
    setvbuf(stdout, 0, _IONBF, 0);
    if((ossHandle= ossOpen())<0)
        return 1;
	while(!game.quit)
        game.tick();
    return 0;
}
