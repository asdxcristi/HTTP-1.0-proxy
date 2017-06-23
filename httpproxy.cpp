#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>


#define MAX_WAITING_CLIENTS	500
#define BUFLEN 10000
#define CACHE_FOLDER "cache"
#define DEFAULT_HTTP_PORT "80"

using namespace std;

int current_connection_fd;

//Inchide threadul curent
void exit_handler(){
  pthread_exit(NULL);
}

/**  KUDOS labu' de PC
* Trimite o comanda HTTP si asteapta raspuns de la server.
* Comanda trebuie sa fie in request_buffer-ul sendbuf.
* Sirul expected contine inceputul raspunsului pe care
* trebuie sa-l trimita serverul in caz de succes (de ex. codul
* 200). Daca raspunsul semnaleaza o eroare se iese din program.
*/
void send_command(int sockfd, string sendbuf) {
  int f=write(sockfd, sendbuf.data(), sendbuf.size());
  //Safeguard
  if(f<0){
    cout<<"No message for u :("<<endl;
    exit_handler();
  }
}

//Citeste din fisierul file_name din cache si trimite raspunsu' la receiver
void read_file(string file_name,int reciever){
  FILE *f = fopen(file_name.data(),"rb");
  if(f==NULL){
    cout<<"[QQ]Fisier Inexistent"<<file_name.data()<<endl;
    exit_handler();
  }

	char buff[BUFLEN];

  while(!feof(f)){
	 bzero(buff,sizeof(buff));
   fread(buff,sizeof(char),sizeof(buff),f);
   string request_buffer(buff);
   send_command(reciever,request_buffer);
  }

  fclose(f);
}

/**  KUDOS labu' de PC
* Citeste maxim maxlen octeti din socket-ul sockfd. Intoarce
* numarul de octeti cititi.
*/
ssize_t Readline(int sockd,void *vptr,size_t maxlen) {
  ssize_t n,rc;
  char c,*request_buffer;

  request_buffer=(char *)vptr;

  for(n=1;(unsigned)n<maxlen;n++){
    if((rc=read(sockd,&c,1))==1){
      *request_buffer++ = c;
      if(c=='\n')
        break;
      if(c=='\0')
        break;
    }else if(rc==0){
      if(n==1)
        return 0;
      else
        break;
    }else{
      if(errno==EINTR)
        continue;
      return -1;
    }
  }

  *request_buffer=0;
  return n;
}

//Initializeaza sistemul de cache
//Creeaza dosarul de cache daca nu exista
//Sterge fisierele din acesta daca exista
void init_cache(){
  DIR *dir;
	struct dirent *ent;
  string path;
  string tmp;

  path=path+"./"+CACHE_FOLDER;//pregatim calea spre folder

  if((dir=opendir(CACHE_FOLDER))!=NULL){//exista deja folderu' de cache
    path+="/";//pregatim calea spre fisier
    //stergem toate fisierele din el
    while((ent = readdir (dir)) != NULL){
      //ignoram folderul curent si parintele din sistemul de fisiere
      if(strcmp(ent->d_name,".")==0 ||strcmp(ent->d_name,"..")==0){
  		    continue;
      }
      tmp=path;
      tmp=tmp+ent->d_name;//bagam fisierul in cale
      //stergem fisieru'
      if(remove(tmp.data())!=0){
  			cout<<"Nu s-a putut sterge fisierul "<<tmp<<endl;
      }
  	}
	  closedir (dir);
  }else{ // Nu exista folderu'
  //Il creeam
    if(mkdir(path.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)<0){
      printf("pufarine\n");
      exit(2);
    }
  }
}

//Verifica daca fisierul cu numele file_name este deja in cache
bool in_cache (string file_name) {
  DIR *dir;
  struct dirent *ent;

  if((dir=opendir(CACHE_FOLDER))!=NULL){
    //cautam prin toate fisirele din folder
    while((ent=readdir(dir))!=NULL){
      //ignoram folderul curent si parintele din sistemul de fisiere
  	   if(strcmp(ent->d_name,".")==0 ||strcmp(ent->d_name,"..")==0 ){
         continue;
       }
       //am gasit fisierul cautat in cache
       if(strcmp(file_name.data(),ent->d_name)==0){
         return true;
       }
    }
    closedir (dir);
  }else{//nu exista folderu'
    perror ("NU s-a deschis/Nu exista folderu' de cache");
    return EXIT_FAILURE;
  }
  return false;
}

//Verifica daca requestul are un format bun
bool check_request(vector<string> request){
  //minim GET http://www.asd.com HTTP/1.0
  if(request.size()<3){
    return false;
  }

  //clasic request methods
  if(request[0]!="GET" && request[0]!="POST" && request[0]!="HEAD"
  //additional request methods
    &&request[0]!="PUT" && request[0]!="DELETE" && request[0]!="LINK "
    &&request[0]!="UNLINK"){cout<<request[0]<<"*"<<endl;
        return false;
    }

    if(request[1]=="/"){//daca e cale relativa
      if(request.size()<5){//fiind relativa, tre sa existe si Host: www.asd.com
        return false;
      }
      if(request[2]!="HTTP/1.0"){//only HTTP/1.0 permited
        return false;
      }
      if(request[3]!="Host:"){//only HTTP/1.0 permited
        return false;
      }
    }else{//nu e cale relativa
      if(request[2]!="HTTP/1.0"){//only HTTP/1.0 permited
        return false;
      }
    }

    return true;
}

//Rutina care se executa pe fiecare thread
//Se ocupa de magia neagra pt fiecare request
//Ia clientul(conexiunea) curent din variabila globala: current_connection_fd
void* request_handler(void* shit=NULL){
  char svr_response[BUFLEN];
  char request_buffer[BUFLEN];
  int n;

  bzero(request_buffer,BUFLEN);
  //scoatem mesajul de la client si vedem daca totu' e OK
  if((n=read(current_connection_fd, request_buffer,sizeof(request_buffer)))<=0){
    if(n==0){
      //conexiunea s-a inchis
      cout<<"Clientul de pe socketul "<<current_connection_fd<<" a murit subit"<<endl;
    }else{
      perror("read cererea");
      exit_handler();
    }
    exit_handler();
  }else{ //am primit ceva bun pe listener
    cout<<"[Request]: "<<request_buffer<<"[/Request]"<<endl;

    bool post=false; //folosit pt a nu baga in cache requesturi de tip POST
    string raw_request(request_buffer);

    //pregatim requestul pentru a putea fi spart si bagat in container cate un
    //"cuvant" per element
    replace(raw_request.begin(),raw_request.end(),'\n',' ');
    replace(raw_request.begin(),raw_request.end(),'\r',' ');

    istringstream request(raw_request);
    string token;

    //bagam reqestu' in containerul HTTP_request cate un "cuvant" per element
    vector<string> HTTP_request;
    while(getline(request,token,' ')){
      HTTP_request.push_back(token);
    }
    //arucam elementele "goale", la gunoi
    HTTP_request.erase(remove(HTTP_request.begin(),HTTP_request.end(),""),HTTP_request.end());

    //verificam daca requestul nu e bun si aruncam la client mesajul cu codu'
    //corespunzator aka 400 Bad Request
    if(!check_request(HTTP_request)){
      string buffer400("HTTP/1.0 400 Bad Request\nContent-Type: text/html\nConnection: close\n\n<html>\n<head><title>400 Bad Request</title></head>\n<body bgcolor=\"white\">\n<center><h1>400 Bad Request</h1></center>\n</body>\n</html>\n");
      send_command(current_connection_fd,buffer400);
      close(current_connection_fd);
    }

    //folosit in logica de cache
    if(HTTP_request[0]=="POST"){
      post=true;
    }

    //daca are parametru Host
    string url;
    string host_url;
    string port(DEFAULT_HTTP_PORT);

    if(raw_request.find("Host:")!=std::string::npos){
      //posibila cale relativa (ex:"GET / HTTP 1.0\n Host: www.asd.com")
      host_url=HTTP_request[4];//adresa folosita pt dns resolve magic
      if(HTTP_request[1]=="/"){//chiar e cale relativa
        url=HTTP_request[4];//luam adresa de la Host
      }else{//e cale absoluta
        url=HTTP_request[1];//luam calea din parametru de dupa metoda
      }
    }else{//nu exista Host => cale absoluta
      url=HTTP_request[1];//luam calea din parametru de dupa metoda
      host_url=HTTP_request[1];//luam calea din parametru de dupa metoda
    }

    //prelucram adresa pt resolver incat sa ajunga la forma acceptata
    //aka www.asd.com sau asd.com NU www.asd.com/definetlynotpr0n
    size_t found=host_url.find("http://");//scoatem http:// daca exista
    if(found!=std::string::npos){
      host_url.replace(found,7,"");
    }

    found=host_url.find(":");//scoatem portu' daca exista acolo
    if(found!=std::string::npos){
      host_url.replace(found,host_url.size()-1,"");
    }

    found=host_url.find("/");//scoatem bucata de link care cere alta resursa
    if(found!=std::string::npos){
      host_url.replace(found,host_url.size()-host_url.find("/")+1,"");
    }

    //prelucram adresa completa intrucat o folosim in logica de cache
    //pe post de nume al resursei salvate

    //il scoatem intrucat e useless pt unicizarea numelor
    found=url.find("http://");
    if(found!=std::string::npos){
      url.replace(found,7,"");
    }
    string full_url=url;

    //Are portu' indesat acolo
    found=url.find(":");
    if(found!=std::string::npos){
      port=url.substr(found+1,url.size()-1);//extragem portu' si il salvam
      url.replace(found,url.size()-1,"");
      full_url=url;
    }

    found=port.find("/");//extrasafe in caz ca portu' mai are alte gunoaie
    if(found!=std::string::npos){
        port.replace(found,port.size()-port.find("/")+1,"");
    }

    //schimbam toate / pe _ pt ca linux filesystem stuff
    // "/" rezervat pt root
    found=url.find("/");
    if(found!=std::string::npos){
      url.replace(found,url.size()-url.find("/")+1,"");
      std::replace(full_url.begin(),full_url.end(),'/','_');
    }

    //costruim calea spre posibila resursa deja cacheuita
    string path;
    path=path+"./"+CACHE_FOLDER+"/"+full_url;

    //verificam daca e in cache si daca e o resursa cacheuibila
    if(in_cache(full_url) && !post){//daca e in cache
      read_file(path,current_connection_fd);//citim & trimitem raspunsu'
      close(current_connection_fd);//inchidem conexiunea
      exit_handler();//inchidem threadu' curent
    }

    struct hostent *he;
    if((he=gethostbyname(host_url.data()))==NULL){//dns resolve magic
        perror("e liber domeniu,du-te si cumpara-l");
        exit_handler();
    }

    struct in_addr **addr_list;
    addr_list = (struct in_addr **)he->h_addr_list;//adresa ip a serverului web

    struct sockaddr_in req_svr;
    int current_fd;
    current_fd=socket(AF_INET,SOCK_STREAM,0);
    if(current_fd<0){
      cout<<"[QQ]Socket TCP spre server"<<endl;
      exit_handler();
    }

    int port_no=atoi(port.data());

    bzero(&req_svr,sizeof(req_svr));

    //indesam informatiile serverului
    req_svr.sin_family=AF_INET;
    req_svr.sin_port=htons(port_no);

    //indesam ipu' extras din resolver
    if(inet_aton(inet_ntoa(*addr_list[0]),&req_svr.sin_addr)<=0){
      cout<<"[QQ]Adresa IP invalida"<<endl;
      exit_handler();
    }

    if(connect(current_fd,(struct sockaddr *)&req_svr,sizeof(req_svr))<0){
      cout<<"Eroare la conectare cu server "<<endl;
      exit_handler();
    }

    string sending(request_buffer);
    send_command(current_fd,sending);//trimitem requestu' la server

    //Indesam in fisier sa ne jucam in browser lol
    FILE *result;
    vector<string> HTTP_response;
    bool cacheable=true;

    bzero(svr_response,BUFLEN);
    //scotem raspunsu' de la server
    while(Readline(current_fd,svr_response,BUFLEN-1)>0){
      HTTP_response.push_back(svr_response);
      //trimitem raspunsu' la client
      send_command(current_connection_fd,HTTP_response[HTTP_response.size()-1]);
      bzero(svr_response,BUFLEN);
    }

    //There's a very special place in HELL :) for dat servers & responses
    if(HTTP_response.size()==0){
      close(current_fd);
      close(current_connection_fd);
      exit_handler();
    }

    //verificam statusu' raspunsului
    found=HTTP_response[0].find("200 OK");
    if(found==std::string::npos){
      cacheable=false;
    }

    //felt like scrolling fata de cautat in vector(mi se pare mai eficient asa)
    for(unsigned int i=0;i<HTTP_response.size();i++){
      if(HTTP_response[i].find("Pragma: ")!=std::string::npos){
        if(HTTP_response[i].find("no-cache")!=std::string::npos){
          cacheable=false;
          break;
        }
      }
      if(HTTP_response[i].find("Cache-Control: ")!=std::string::npos){
        //HTTP 1.1 stuff, nu e in RFC, still e in cerinta
        if(HTTP_response[i].find("private")!=std::string::npos){
          cacheable=false;
          break;
        }
        if(HTTP_response[i].find("no-cache")!=std::string::npos){
          cacheable=false;
          break;
        }
        if(HTTP_response[i].find("no-store")!=std::string::npos){
          cacheable=false;
          break;
        }
      }
    }

    //CHECK IF CACHEABLE
    if(cacheable){
      result=fopen(path.data(),"wb");
      if(result==NULL){
        cout<<"[QQ]Nu s-a deschis fisieru' pt scris in cache"<<endl;
        exit_handler();
     }
     //scriem raspunsu' in fisieru' de cache
      for(unsigned int i=0;i<HTTP_response.size();i++){
        fprintf(result,"%s",HTTP_response[i].data());
      }

      fclose(result);
    }
    //inchidem conexiunile client->proxy si proxy->server
    close(current_fd);
    close(current_connection_fd);
    exit_handler();

  }
  //fuck da warning
  void* ret=NULL;
  return ret;
}


int main(int argc,char *argv[]){
  //Safeguard pentru argumentele de rulare
  if(argc!=2){
    fprintf(stderr,"Usage : %s <PORT> \n", argv[0]);
    exit(1);
  }

  int tcp_fd,port;
  socklen_t client_len;
  struct sockaddr_in serv_addr, cli_addr;

  tcp_fd=socket(AF_INET,SOCK_STREAM,0);
  if(tcp_fd<0){
    cout<<"[QQ]Socket TCP listener"<<endl;
    exit(0);
  }

  port=atoi(argv[1]);
  bzero(&serv_addr,sizeof(serv_addr));

  //indesam infromatiile serverlui
  serv_addr.sin_family=AF_INET;
  serv_addr.sin_addr.s_addr=INADDR_ANY;
  serv_addr.sin_port=htons(port);

  //bind-uim socketul TCP
  if(bind(tcp_fd,(struct sockaddr*)&serv_addr,sizeof(struct sockaddr))<0){
    cout<<"[QQ]Bind TCP listener"<<endl;
    exit(0);
  }

  //punem socketul TCP pe listening
  listen(tcp_fd,MAX_WAITING_CLIENTS);

  //initializam logica de cache
  init_cache();

  while(1){
    //am primit ceva pe listenerul TCP pt conexiuni noi
    client_len=sizeof(cli_addr);
    if((current_connection_fd=accept(tcp_fd,(struct sockaddr*)&cli_addr,&client_len))<0){
      //too lazy to check every errno
      //asa ca trecem mai departe fara sa oprim tot si asteptam sa se faca loc
      cout<<"[QQ]maybe full"<<endl;
    }else{
      pthread_t new_thread;
      if(pthread_create(&new_thread, NULL, request_handler, NULL)){
        cout<<"[QQ]thread couldn't be born"<<endl;
      }
    }
  }
}
