#include<sys/types.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<string.h>
#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<stdarg.h>
#include<rpc/types.h>
#include<time.h>
#include<sys/param.h>
#include<getopt.h>
#include<sys/wait.h>
#include<pthread.h>
#include<sys/mman.h>
#define MAX_REQUEST 4096        
#define MAX_RESPONSE 4096

enum {
	GET_METHOD,POST_METHOD
};
enum{
	HTTP09,HTTP10,HTTP11
};
int http_method=-1,http_version=HTTP11;

const struct option long_options[]={
	{"header",1,NULL,'h'},
	{"port",1,NULL,'p'},
//	{"address",1,NULL,'a'},
	{"data",1,NULL,'d'},
	{"help",0,NULL,'?'},
	{"uri",1,NULL,'u'},
	{"post",0,&http_method,POST_METHOD},
	{"get",0,&http_method,GET_METHOD},
	{"http09",0,&http_version,HTTP09},
	{"http10",0,&http_version,HTTP10},
	{"http11",0,&http_version,HTTP11},
	{"clientNum",1,NULL,'c'},
	{0,0,0,0}
};

char *CRLF="\r\n";
pthread_mutex_t* mutex;
struct timespec *used_time;


int cli_socket(const char* hostaddr,int port){
	int sockfd;
	struct sockaddr_in serveraddr;
	
	memset(&serveraddr,0,sizeof(serveraddr));
	if((sockfd=socket(AF_INET,SOCK_STREAM,0))<0){
		perror("socket error ");
		return -1;
	}
	serveraddr.sin_family=AF_INET;
	serveraddr.sin_port=htons(port);
	if(inet_pton(AF_INET,hostaddr,&serveraddr.sin_addr)<0){
		perror("inet_pton error");
		return -1;
	}
	if(connect(sockfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr))){
		perror("connect error");	
		return -1;
	}
	return sockfd;
}
void send_request(const char* req,const char* hostaddr,int port,char* res,int res_size){
	struct timespec start,end,spend,temp;
	int sockfd,req_len=strlen(req)+1,total_send=0,remaind=0,num=0;

	//printf("%d res_size",res_size);
	if((sockfd=cli_socket(hostaddr,port))<0){
		perror("cli_sokcet error!");
		exit(2);
	}
	//avoid miss some data 
	remaind=req_len;
	while((num=send(sockfd,req+total_send,remaind,0))>0){
		total_send+=num;
		//TODO see unix network programming to make sure how to solve the case if send return 0
		remaind-=num;	
	}
	if(num<0){
          	perror("send error!");
              	exit(2);
      	}
	shutdown(sockfd,1);//TODO add if to avoid error
	//recv(sockfd,res,res_size,0);
	clock_gettime(CLOCK_MONOTONIC,&start);
	while((num=recv(sockfd,res,(ssize_t)res_size,0))>0){
		res[num]='\0';
		res_size-=num;
		res+=num;
	}
	clock_gettime(CLOCK_MONOTONIC,&end);
	if((temp.tv_nsec=end.tv_nsec-start.tv_nsec)<0){
		spend.tv_nsec=1e9+temp.tv_nsec;
		spend.tv_sec=end.tv_sec-start.tv_sec-1;
	}
	else{
		spend.tv_nsec=temp.tv_nsec;
		spend.tv_sec=end.tv_sec-start.tv_sec;
	}	
	if(num<0){
                perror("send error!");
                exit(2);
        }
	close(sockfd);

	pthread_mutex_lock(mutex);
	used_time->tv_nsec+=spend.tv_nsec;
	if((used_time->tv_nsec%(long)1e9)>0)
		used_time->tv_sec+=1;
	used_time->tv_nsec = used_time->tv_nsec%(long)1e9;
	used_time->tv_sec+=spend.tv_sec;
	pthread_mutex_unlock(mutex);
}
void cat_request(char* req,char* uri,char* http_header,char* data,char* host_addr){
	char* temp;
	switch(http_method){
		case GET_METHOD:
			strcat(req,"GET ");
			break;
		case POST_METHOD:
			strcat(req,"POST ");
			break;
	}
	strcat(req,uri);
	strcat(req," HTTP/");
	switch(http_version){
		case HTTP09:
			strcat(req,"0.9");
			break;
		case HTTP10:
			strcat(req,"1.0");
                        break;	
		case HTTP11:
			strcat(req,"1.1");
                        break;
	}
	strcat(req,CRLF);
	strcat(req,"HOST: ");
	strcat(req,host_addr);
	strcat(req,CRLF);
	if(http_header!=NULL){
		temp=strtok(http_header,";");
		while(temp!=NULL){
			strcat(req,temp);
			strcat(req,CRLF);
			temp=strtok(NULL,";");
		}
	}
	strcat(req,CRLF);
	if(data!=NULL){
		strcat(req,CRLF);
		temp=strtok(data,";");
        	while(temp!=NULL){
            	    	strcat(req,temp);
             		strcat(req,CRLF);
			temp=strtok(NULL,";");
        	}
	}
}
void child_handler(int sig){
	int status;
	wait(&status);
}
int main(int argc,char** argv){	
	int server_port=80,opt,pid,i,cli_num=1;
	int option_index=0,bit_wise=3;
	//char *re="GET /register? HTTP/1.1\r\nHost: 127.0.0.1:3000\r\nConnection: keep-alive\r\n\r\n";
	char request[MAX_REQUEST];
	char response[MAX_RESPONSE];
	char* uri=NULL,*http_header=NULL,*data=NULL,*addr=NULL,*temp=NULL,*addr_temp=NULL;
	used_time=NULL;
	pthread_mutexattr_t mutex_attr;
	
	//initialize shared memory and mutex
	mutex=(pthread_mutex_t*)mmap(NULL,sizeof(pthread_mutex_t),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
	used_time=(struct timespec*)mmap(NULL,sizeof(struct timespec),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
	
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setpshared(&mutex_attr,PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(mutex,&mutex_attr);
	
	used_time->tv_sec=0;
	used_time->tv_nsec=0;	

	memset(request,0,sizeof(request));
	memset(response,0,sizeof(response));
	//TODO add check input length to avoid error
	//parse argv  
	while((opt=getopt_long(argc,argv,"h:p:d:?u:c:a:",long_options,&option_index))!=-1){
		switch(opt){
			case 'u': //need
				bit_wise^=1<<0;
				uri=strdup(optarg);
				temp=strstr(optarg,"://");
				if(temp==NULL){
					printf("uri format error!\n");//may change to print to stderr
					exit(1);
				}
				temp+=3;
				if((addr_temp=strtok(temp,"/"))==NULL){
					printf("uri format error!\n");
					exit(1);
				}
				addr=strdup(addr_temp);
				printf("host address : %s\n",addr);
				break;
	/*		case 'a':
				addr=strdup(optarg);
				break;*/
			case 'h':
			//	printf("%s\n",optarg);
				
				http_header=strdup(optarg);
				break;
			case 'p'://need
			//	printf("%s\n",optarg);
				//01
				bit_wise^=1<<1;
				server_port=atoi(optarg);
                                break;
			case 'd':
			//	printf("%s\n",optarg);
				data=strdup(optarg);
                                break;
			case 'c':
				cli_num=atoi(optarg);
				break;
			case '?':
				perror("help or your input error!");
				exit(1);
				break;
		}
	}
	if(bit_wise!=0){
		switch(bit_wise){
			case 3:
				perror("lack of uri and port");
				exit(1);
				break;
			case 2:
				perror("lack of port");
				exit(1);
				break;
			case 1:
				perror("lack of uri");
				exit(1);
				break;
		}
	}
	//buid your request
	cat_request(request,uri,http_header,data,addr);
	
	printf("request\n%s\n",request);
	//signal(SIGCHLD,child_handler);
	for(i=0;i<cli_num;i++){
		if((pid=fork())<0){
			perror("fork error!");
			exit(2);
		}
		if(pid==0){
			sleep(1);
			break;
		}
	}
	if(pid==0){
		send_request(request,addr,server_port,response,sizeof(response));
		printf("response\n%s\n",response);
	}
	if(pid>0){
		int status;
		for(i=0;i<cli_num;i++)
			wait(&status);
		printf("spend time : %ld.%ld(nsec)\n",used_time->tv_sec,used_time->tv_nsec);
	}
	free(uri);
	free(http_header);
	free(data);
	free(addr);	
	return 0;
}

