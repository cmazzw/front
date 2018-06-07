#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include <curl.h>
#include <sys/msg.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <dirent.h>
#include <unistd.h>

/*
    远端如果有同名文件，则会覆盖同名文件
    远端如果有同名的.tmp文件，则会追加到这个文件的结尾
*/

#define MAX_LEN sizeof(job)

typedef struct job_st
{
  char job_local_di[512];//like "/home/zzw/curl/text.txt"
  char job_host_auth[64];//like "zzw:zhang113934"
  char job_url_di[512];//like "ftp://172.19.112.3//behb/diqu/test/text.txt.tmp"
  char job_rnfr_di[128];//"RNFR text.txt.tmp"
  char job_rnto_di[128];//"RNTO text.txt"
  int  job_ftp_mode;//1表示被动传输  0表示主动传输
}job;

//ftp日志记录
int ftp_log(CURL *curl, curl_infotype type, char *str, size_t size, void* stream)
{
   if ( CURLINFO_TEXT == type ||CURLINFO_HEADER_IN == type||CURLINFO_HEADER_OUT == type)
    {
       fwrite(str, 1, size, (FILE *)stream);
    }
}

//消息队列结构
typedef struct msg_st
{
  long int msg_type;
  job msg_job;
}job_msg;

//头信息获取的回调函数
size_t getcontentlengthfunc(void *ptr, size_t size, size_t nmemb, void *stream)
{
  int r;
  long len = 0;

  r = sscanf((const char*)ptr,"Content-Length: %ld\n", &len);

  if(r) 
    *((long *) stream) = len;

  return size * nmemb;
}

//取消传输的回调函数
size_t discardfunc(void *ptr, size_t size, size_t nmemb, void *stream)
{
  return size * nmemb;
}

//读取传输的回调函数
size_t readfunc(void *ptr, size_t size, size_t nmemb, void *stream)
{
  FILE *f = stream;
  size_t n;

  if(ferror(f))
    return CURL_READFUNC_ABORT;

  n = fread(ptr, size, nmemb, f) * size;

  return n;
}

//上传函数
int upload(job para,int timeout,int tries,FILE *fp)
{
  int return_value;

  CURL *curlhandle = NULL;

  curl_global_init(CURL_GLOBAL_ALL);
  curlhandle = curl_easy_init();
  if(curlhandle==NULL)
    {
      return 0;
    }

  FILE *f;
  long uploaded_len = 0;
  CURLcode r = CURLE_GOT_NOTHING;
  int c;

  struct curl_slist *renamelist=NULL;

  f = fopen(para.job_local_di, "rb");
  if(!f) 
  {
    perror(NULL);
    return 0;
  }

  renamelist = curl_slist_append(renamelist,para.job_rnfr_di);
  renamelist = curl_slist_append(renamelist,para.job_rnto_di);
  curl_easy_setopt(curlhandle, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(curlhandle, CURLOPT_URL, para.job_url_di);
  curl_easy_setopt(curlhandle,CURLOPT_USERPWD,para.job_host_auth);
  if(timeout)
    curl_easy_setopt(curlhandle, CURLOPT_FTP_RESPONSE_TIMEOUT, timeout);
  curl_easy_setopt(curlhandle, CURLOPT_HEADERFUNCTION, getcontentlengthfunc);
  curl_easy_setopt(curlhandle, CURLOPT_HEADERDATA, &uploaded_len);
  curl_easy_setopt(curlhandle, CURLOPT_WRITEFUNCTION, discardfunc);
  curl_easy_setopt(curlhandle, CURLOPT_READFUNCTION, readfunc);
  curl_easy_setopt(curlhandle, CURLOPT_POSTQUOTE, renamelist);//上传tmp文件
  curl_easy_setopt(curlhandle, CURLOPT_DEBUGDATA, fp);
  curl_easy_setopt(curlhandle, CURLOPT_DEBUGFUNCTION, ftp_log);
  curl_easy_setopt(curlhandle, CURLOPT_READDATA, f);
  if(para.job_ftp_mode)
     curl_easy_setopt(curlhandle, CURLOPT_FTPPORT, NULL);
  else
     curl_easy_setopt(curlhandle, CURLOPT_FTPPORT, "-"); 
  curl_easy_setopt(curlhandle, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
  curl_easy_setopt(curlhandle, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curlhandle, CURLOPT_APPEND, 1L);


  for(c = 0; (r != CURLE_OK) && (c < tries); c++) 
  {
    r = curl_easy_perform(curlhandle);
  }

  fclose(f);

  if(r != CURLE_OK)
    return_value=0;
  else
    return_value=1;

  curl_easy_cleanup(curlhandle);
  curl_slist_free_all (renamelist);
  curl_global_cleanup();

  return return_value;
}

//ftp失败的记录函数
int faildjob(job para)
{
  //获取当前时间
  time_t timer_reload;//time_t就是long int 类型
  struct tm  *tm_reload;//存储时间的结构体
  timer_reload = time(NULL);
  tm_reload = localtime(&timer_reload);//获取了时间结构体

  char log_file[128];

  sprintf(log_file,"../reload/%d%02d%02d%02d.log",tm_reload->tm_year+1900,tm_reload->tm_mon+1,tm_reload->tm_mday,tm_reload->tm_hour);
  FILE *fp;
  fp=fopen(log_file, "at");
  if(fp==NULL)
    {
       return 0;
    }
  fprintf(fp,"%s,%s,%s,%s,%s,%d\n",para.job_local_di,para.job_host_auth,para.job_url_di,para.job_rnfr_di,para.job_rnto_di,para.job_ftp_mode);
  fclose(fp);
  return 1;
}


int main(void)
{
   int msgid;
   job_msg msg_di;

   msgid=msgget((key_t)1235,0666 | IPC_CREAT);
   if(msgid==-1)
    {
       
       fprintf(stderr,"msgget failed with error :%d\n",errno);
       exit(EXIT_FAILURE);
    }

    while(1)
     {
         printf("now recv msg\n");
         //获取当前时间
         time_t timer;//time_t就是long int 类型
         struct tm  *tm_struct;//存储时间的结构体
         timer = time(NULL);
         tm_struct = localtime(&timer);//获取了时间结构体
         
         
        //建立日志文件，并打开
        char log_file[128];
        char log_path[32];
        sprintf(log_path,"/home/filecom/log");
        DIR *dp_log_test=opendir(log_path);
        if(NULL==dp_log_test)
           mkdir(log_path,0775);
        else
           closedir(dp_log_test); 
        sprintf(log_file,"%s/send_pid%d_%d%02d%02d.log",log_path,getpid(),tm_struct->tm_year+1900,tm_struct->tm_mon+1,tm_struct->tm_mday);
        FILE *fp;
        if((fp=fopen(log_file,"at"))==NULL)
         {
           printf("open log faild \n");
           return 0;
         }

        int status; 
        if((status=msgrcv(msgid,(void *)&msg_di,MAX_LEN,0,0))==-1)
          {
            fprintf(fp,"msgrcv failed with error :%d\n",errno);
            exit(EXIT_FAILURE);
          }
        fprintf(fp,"%s,%s,%s,%s,%s,%d\n",msg_di.msg_job.job_local_di,msg_di.msg_job.job_host_auth,msg_di.msg_job.job_url_di,msg_di.msg_job.job_rnfr_di,msg_di.msg_job.job_rnto_di,msg_di.msg_job.job_ftp_mode);
        int ftp_status=upload(msg_di.msg_job,0,3,fp);
        if(ftp_status==1)
         {
             fprintf(fp,"ftp sucessful!");
             if(remove(msg_di.msg_job.job_local_di)==0)
                 {
                   //fprintf(fp,"ftp sucessful!");
                   printf("%s is remove",msg_di.msg_job.job_local_di);
                 }
         }
        else
         {
             //记录失败的传输
             int faildstatus=faildjob(msg_di.msg_job);
             fprintf(fp,"ftp failed!");
         }
        fprintf(fp,"\n");
        fclose(fp);
     }
}
