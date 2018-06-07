#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include <curl.h>
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

  sprintf(log_file,"/home/filecom/reload/%d%02d%02d%02d%02d%02d.log",tm_reload->tm_year+1900,tm_reload->tm_mon+1,tm_reload->tm_mday,tm_reload->tm_hour,tm_reload->tm_min,tm_reload->tm_sec);
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


int main(int argc,char **argv)
{
    //定义失败传输所在的日志全路径
    char reloadfile[64];
    //从程序参数中获取文件的全路径
    strcpy(reloadfile,argv[1]);

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
    sprintf(log_file,"%s/reload%d%02d%02d%02d%02d%02d.log",log_path,tm_struct->tm_year+1900,tm_struct->tm_mon+1,tm_struct->tm_mday,tm_struct->tm_hour,tm_struct->tm_min,tm_struct->tm_sec);
    FILE *fp,*fp_file;
    if((fp=fopen(log_file,"at"))==NULL)
      {
        printf("open log faild \n");
        return 0;
      }

    //打开reload文件，获取一行数据
     if( (fp_file=fopen(reloadfile,"r"))==NULL)
        {
           printf("open reloadfile faild \n");
           return(0);
        }

     char buf[2048];
     while(fgets(buf,2048,fp_file)!=NULL)
        {
           job line_job;
           char *delim = ",";
           strcpy(line_job.job_local_di,strtok(buf, delim));
           strcpy(line_job.job_host_auth,strtok(NULL, delim));
           strcpy(line_job.job_url_di,strtok(NULL, delim));
           strcpy(line_job.job_rnfr_di,strtok(NULL, delim));
           strcpy(line_job.job_rnto_di,strtok(NULL, delim));
           sscanf(strtok(NULL,delim),"%d",&line_job.job_ftp_mode);
 
           fprintf(fp,"%s,%s,%s:",line_job.job_local_di,line_job.job_url_di,line_job.job_rnto_di);
           printf("%s,%s,%s:",line_job.job_local_di,line_job.job_url_di,line_job.job_rnto_di);
           int ftp_status=upload(line_job,0,3,fp);
           if(ftp_status==1)
           {
             fprintf(fp,"status=ftp sucessful!\n");
             printf("status=ftp sucessful!\n");
             //删除本地的文件
             if(remove(line_job.job_local_di)==0)
                 {
                   printf("%s is remove\n",line_job.job_local_di);
                 }
           }
           else
           {
             //记录失败的传输
             int faildstatus=faildjob(line_job);
             fprintf(fp,"status=ftp failed!\n");
             printf("status=ftp failed!\n");
           }
         }
      fclose(fp_file);
      //删除这个文件
      if(remove(reloadfile)==0)
        {
           printf("%s is remove\n",reloadfile);
        }
      fclose(fp);
}
