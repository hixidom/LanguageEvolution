//g++ -o LangEvolve#.# LangEvolve#.#.cpp glut32.lib -lopengl32 -lglu32 -lopenal32 -static-libgcc -static-libstdc++  //use this format to compile
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <windows.h>
#include "GL/glut.h"
#include <cmath>
#include <vector>
using namespace std;

#define BUILDID "1.31"
/*Changes:
    - Bots are no longer unable to eat other living members of same type.
    - Type discrimination is now done by comparison of output algorithm.
    - Quantity of random bots is now held constant.
    - Bugfix: INPUTNUM had not been adjusted for type dinstinction.
*/

#define TWOPI 6.28319

//Simulation parameters
#define MAXLINVELOCITY 2*BOTSIZE
#define SIGHTRANGE 10*BOTSIZE
#define BOTSIZE .04
#define BOTNUM 40
#define MENTALSIZE 0
#define CHARACTERLIMIT 0
#define SIGHTFOV (TWOPI/4)
#define HEARINGRANGE SIGHTRANGE
#define RAYNUM 3
#define EATRATE .05
#define DECAYRATE 5*AGERATE
#define AGERATE .0002
#define BURNRATE .1
#define MAXANGVELOCITY 1

#define HIDDENNUM (int)((INPUTNUM+OUTPUTNUM)/2)
#define INPUTNUM (1+4+2*(RAYNUM-1)+2*((RAYNUM-1)/2)+CHARACTERLIMIT+MENTALSIZE+1)
#define OUTPUTNUM (3+CHARACTERLIMIT+MENTALSIZE)

#define INPUTSTR "1\tray0 health\tray0 type\tray0 r\tray0 diffang\tray+1 health\tray-1 health\tray+1 type\tray-1 type\tray+1r - ray-1r\tray+1diffang + ray-1diffang\tHealth"
#define OUTPUTSTR "eat\ttranslate\trotate"

#define ALGSTDEV (.3/OUTPUTNUM)

GLint winw=600,winh=600;
GLfloat viewradius=1.5;
int t=0,recordage=0,logtimestamp;
bool dispbit=true,pausebit=false;

float randnorm(float SD){
    return SD*sqrt(-2*log(((rand()%10000)+1)/10001.0))*cos(TWOPI*(rand()%10000)/10000.0);
}

class seentype{
    public:
    float r,diffangle;
    int index;
    seentype(){
        index=-1;
        r=0.001;
    }
};

class datalogtype{
    public:
    vector<float> logtime;
    vector<int> age,pop;
    void add(int tempt,int a,int p){
        logtime.push_back(log(tempt));
        age.push_back(a);
        pop.push_back(p);
    }
    int size(){
        return logtime.size();
    }
}datalog;

class bottype{
    public:
        float algin[HIDDENNUM][INPUTNUM],algout[HIDDENNUM][OUTPUTNUM],health,x,y,theta,mentalstate[MENTALSIZE];
        unsigned int speechqueue[CHARACTERLIMIT],type,age,gen;
        
        void clearspeech(){
            int i;
            for(i=0;i<CHARACTERLIMIT;i++)
            speechqueue[i]=0;
        }
        
        void resetall(){
            gen=0;
            age=0;
            clearspeech();
            health=2;
            x=2*(rand()%1000)/1000.0-1;
            y=2*(rand()%1000)/1000.0-1;
            theta=TWOPI*(rand()%1000)/1000.0;
            
            //type=rand()%100000;        
            int i,h;
            for(i=0;i<INPUTNUM;i++)
                for(h=0;h<HIDDENNUM;h++)
                    algin[h][i]=randnorm(ALGSTDEV);
            for(i=0;i<OUTPUTNUM;i++)
                for(h=0;h<HIDDENNUM;h++)
                    algout[h][i]=randnorm(ALGSTDEV);
        }
        
        void mutate(int num){
            int n;
            gen++;
            for(n=0;n<num;n++){
                if(rand()%2)algin[rand()%HIDDENNUM][rand()%INPUTNUM]+=randnorm(0.04*ALGSTDEV);
                else algout[rand()%HIDDENNUM][rand()%OUTPUTNUM]+=randnorm(0.04*ALGSTDEV);
            }
            age=0;
        }
               
        bottype(){
            resetall();
        }
        
        float dist(float testx,float testy){
            return sqrt(pow(testx-x,2)+pow(testy-y,2));
        }
        
        vector<float> applyalg(vector<float> input){
            vector<float> output;
            vector<float> hidden;
            int i,h;
            for(h=0;h<HIDDENNUM;h++){
                hidden.push_back(0);
                for(i=0;i<INPUTNUM;i++)
                    hidden.back()=hidden.back()+algin[h][i]*input[i];
            }
            for(i=0;i<OUTPUTNUM;i++){
                output.push_back(0);
                for(h=0;h<HIDDENNUM;h++)
                    output.back()=output.back()+algout[h][i]*tanh(hidden[h]);
            }
            return output;
        }
};

class populationtype{
    vector<bottype> bots;
    bottype champ;
    
    void writedatalog(char *filename){
        int i,h;
        ofstream ofile(filename,ios::trunc);
        ofile<<INPUTSTR<<"\n";
        for(h=0;h<HIDDENNUM;h++){
            for(i=0;i<INPUTNUM-1;i++)ofile<<champ.algin[h][i]<<"\t";
            ofile<<champ.algin[h][INPUTNUM-1]<<"\n";
        }
        ofile<<"\n";
        for(h=0;h<HIDDENNUM;h++){
            for(i=0;i<OUTPUTNUM-1;i++)ofile<<champ.algout[h][i]<<"\t";
            ofile<<champ.algout[h][INPUTNUM-1]<<"\n";
        }
        ofile<<"\nLn(time)\trecord age\tpopulation";
        for(i=0;i<datalog.size();i++)ofile<<"\n"<<datalog.logtime[i]<<"\t"<<datalog.age[i]*AGERATE<<"\t"<<datalog.pop[i];
        ofile.close();
    }
    
    float algrms(int a,int b){
        if(a==-1 || b==-1)return -1;
        
        float rms=0;
        int o,h;
        for(o=0;o<OUTPUTNUM;o++){
            for(h=0;h<HIDDENNUM;h++){
                rms+=pow(bots[a].algout[h][o]-bots[b].algout[h][o],2);
            }
        }
        return sqrt(rms);
    }
    
    seentype rayblocked(short i,float rayangle,float rayseparation){               //Check an object along a ray defined by the ith bot's position and an angle
        int j;
        float rstep;
        seentype seen;
        while(seen.index==-1 && seen.r<SIGHTRANGE){
            for(j=0;j<bots.size();j++){
                if(j==i || bots[j].health<=-1)continue;
                if(bots[j].dist(bots[i].x+seen.r*cos(bots[i].theta+rayangle),bots[i].y+seen.r*sin(bots[i].theta+rayangle))<seen.r*tan(rayseparation/2)){
                    seen.index=j;
                    seen.diffangle=fmod(bots[j].theta-bots[i].theta,TWOPI);
                    if(seen.diffangle>TWOPI/2)seen.diffangle-=TWOPI;   
                    break;
                }
            }
            seen.r+=2*seen.r*tan(rayseparation/2);
        }
        if(seen.index==-1){
            seen.r=0;
            seen.diffangle=0;
        }                         
        return seen;
    }
    
    void fission(int b){
        bots[b].health=2;
        bots.push_back(bots[b]);
        bots.back().mutate(rand()%3);
        bots.back().age=0;
        bots.back().theta+=TWOPI/2.1;
    }
    
    public:
        void dispinfo(){
            cout<<"\nTime: "<<t<<"\tRecord Age: "<<recordage*AGERATE<<"\tPopulation: "<<bots.size();
        }
        
        void reset(){
            int j;
            for(j=0;j<BOTNUM;j++){
                bottype newbot;
                bots.push_back(newbot);}
        }
            
        populationtype(){
            reset();
        }
        
        void timestep(){
            int i;
            for(i=bots.size()-1;i>=0;i--){
                if(bots[i].health<=0){
                    if(bots[i].age==recordage){
                        datalog.add(t,recordage,bots.size());
                        dispinfo();
                        char str[25];
                        sprintf(str,"V%s Log %i.txt",BUILDID,logtimestamp);
                        writedatalog(str);
                    }
                    if(bots[i].gen==0)bots[i].resetall();
                    else{
                        bots.erase(bots.begin()+i);
                        continue;
                    }
                }
                else if(bots[i].health<=1){
                    bots[i].health-=DECAYRATE; //Decay
                    continue;
                }
                bots[i].health-=AGERATE; //Natural aging
                bots[i].age++;
                if(bots[i].age>recordage){
                    recordage=bots[i].age;
                    champ=bots[i];
                }
                //Gather inputs
                    //Sight inputs
                    vector<float> inputs;
                    float angle;
                    seentype seen,seen2;
                    inputs.push_back(1); //A linear equation may also contain a constant
                        //Straight ahead
                        seen=rayblocked(i,0,SIGHTFOV/(RAYNUM-1));
                        if(seen.index==-1)inputs.push_back(-1);
                        else inputs.push_back(bots[seen.index].health);
                        inputs.push_back(algrms(i,seen.index));
                        inputs.push_back(seen.r);
                        float diffangle;
                        diffangle=fmod(bots[seen.index].theta-bots[i].theta,TWOPI);
                        if(diffangle<TWOPI/2)inputs.push_back(diffangle);
                        else inputs.push_back(diffangle-TWOPI);
                        if(seen.index==-1)
                        inputs.push_back(bots[i].type-bots[seen.index].type);
                        //Peripherals
                        for(angle=SIGHTFOV/(RAYNUM-1);angle<=SIGHTFOV/2+(SIGHTFOV/4)/(RAYNUM-1);angle+=SIGHTFOV/(RAYNUM-1)){
                            seen=rayblocked(i,angle,SIGHTFOV/(RAYNUM-1));
                            seen2=rayblocked(i,-angle,SIGHTFOV/(RAYNUM-1));
                            if(seen.index==-1)inputs.push_back(-1);
                            else inputs.push_back(bots[seen.index].health);
                            if(seen2.index==-1)inputs.push_back(-1);
                            else inputs.push_back(bots[seen2.index].health);
                            inputs.push_back(algrms(i,seen.index));
                            inputs.push_back(algrms(i,seen2.index));
                            inputs.push_back(seen.r-seen2.r);
                            inputs.push_back(seen.diffangle+seen2.diffangle);
                            inputs.push_back(bots[i].type-bots[seen.index].type);
                            inputs.push_back(bots[i].type-bots[seen2.index].type);
                            
                        }
                    //Sound inputs
                    int j;
                    for(j=0;j<bots.size();j++){
                        if(j==i || bots[j].health<=0 || t==0)continue;
                        if(bots[j].dist(bots[i].x,bots[i].y)>HEARINGRANGE)continue;
                        int c;
                        for(c=0;c<CHARACTERLIMIT;c++)inputs.push_back(bots[j].speechqueue[c]);
                        break; //Listen to one other bot only
                    }
                    //Health input
                    inputs.push_back(bots[i].health);
                    //Mental state input
                    for(j=0;j<MENTALSIZE;j++)inputs.push_back(bots[i].mentalstate[j]);
                //Create outputs
                    vector<float> outputs=bots[i].applyalg(inputs);
                    //cout<<outputs[0]<<", "<<outputs[1]<<", "<<outputs[2]<<"\n";system("pause");
                //Apply outputs
                    if(outputs[0]>0){
                        seen=rayblocked(i,0,(TWOPI/4)/(RAYNUM-1));
                    }
                        if(seen.index>=0 && seen.r<2*BOTSIZE && (bots[i].type!=bots[seen.index].type || bots[seen.index].health<1)){
                            bots[seen.index].health-=EATRATE;
                            bots[i].health+=EATRATE;
                        }
                    bots[i].x+=MAXLINVELOCITY*(tanh(outputs[1]-.5)+tanh(.5))*cos(bots[i].theta);
                    bots[i].y+=MAXLINVELOCITY*(tanh(outputs[1]-.5)+tanh(.5))*sin(bots[i].theta);
                    float h=bots[i].health;
                    bots[i].health-=BURNRATE*MAXLINVELOCITY*abs(tanh(outputs[1]-.5)+tanh(.5));
                    bots[i].theta+=MAXANGVELOCITY*tanh(outputs[2]);
                    bots[i].health-=BURNRATE*MAXANGVELOCITY*abs(tanh(outputs[2]))/10;
                    for(j=0;j<CHARACTERLIMIT;j++)bots[i].speechqueue[j]=outputs[3+j];
                    for(j=0;j<MENTALSIZE;j++)bots[i].mentalstate[j]=outputs[3+CHARACTERLIMIT+j];
                
                if(bots[i].health>=4)fission(i);
            }
            t++;
        }
        
        void disp(){
        	glClear(GL_COLOR_BUFFER_BIT);		     // Clear Screen and Depth Buffer
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            gluOrtho2D(-viewradius,viewradius,viewradius,-viewradius);
            int i;
        	for(i=0;i<bots.size();i++){      
                if(bots[i].health<=0)continue;
                else if(bots[i].health>1)glColor3f(1,0.2+(bots[i].health-1)*0.8,0.2+(bots[i].health-1)*0.8);
                else if(bots[i].health>0)glColor3f(0.2+bots[i].health*0.8,0,0);
                glBegin(GL_TRIANGLES);
                    glVertex2f(bots[i].x-BOTSIZE*sin(bots[i].theta)/3,bots[i].y+BOTSIZE*cos(bots[i].theta)/3);
                    glVertex2f(bots[i].x+BOTSIZE*cos(bots[i].theta),bots[i].y+BOTSIZE*sin(bots[i].theta));
                    glVertex2f(bots[i].x+BOTSIZE*sin(bots[i].theta)/3,bots[i].y-BOTSIZE*cos(bots[i].theta)/3);
                glEnd();
            }
            glutSwapBuffers();
        }
}population;

void resize(int x,int y){
    if(x>0)winw=x;
    if(y>0)winh=y;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-viewradius,viewradius,viewradius,-viewradius);
}

void disp() 
{
    if(dispbit)population.disp();
    if(!pausebit)population.timestep();
}

void init() 
{
    srand(time(NULL));
	glViewport(0,0,winw,winh);
	glClearColor(0.0, 0.0, 0.0, 1.0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-viewradius,viewradius,viewradius,-viewradius);
    logtimestamp=time(NULL);
    cout<<"\nSimulation timestamp = "<<logtimestamp;
}

void keyfunc(unsigned char key,int x,int y){
    switch(key){
        case ' ': dispbit=!dispbit; break;
        case 'p': pausebit=!pausebit; break;
        case '=': viewradius/=1.2; break;
        case '-': viewradius*=1.2; break;
        case 'r': population.dispinfo(); break;
    }
}

int main(int argc, char **argv) 
{
	glutInit(&argc, argv);                                                      // GLUT initialization
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);                                // Display Mode
	glutInitWindowSize(winw,winh);					                            // set window size
	glutCreateWindow("Phase 1 Evolution Simulation");							// create Window
	glutDisplayFunc(disp);									                    // register Display Function
	glutIdleFunc(disp);
	glutReshapeFunc(resize);
	glutKeyboardFunc(keyfunc);
	//glutSpecialFunc(specialkeyfunc);
	//glutKeyUpFunc(keyupfunc);
	init();
	glutMainLoop();												                // run GLUT mainloop
	return 0;
}
