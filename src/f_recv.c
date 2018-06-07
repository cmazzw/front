#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <errno.h>
#include <unistd.h>
#include "../lib/libxml/include/libxml/tree.h"
#include "../lib/libxml/include/libxml/parser.h"

#define MAX_LEN sizeof(job)

//定义ftp作业的数据结构体
typedef struct job_st
{
  char job_local_di[512];//like "/home/zzw/curl/text.txt"
  char job_host_auth[64];//like "zzw:zhang113934"
  char job_url_di[512];//like "ftp://172.19.112.3//behb/diqu/test/text.txt.tmp"
  char job_rnfr_di[128];//"RNFR text.txt.tmp"
  char job_rnto_di[128];//"RNTO text.txt"
  int  job_ftp_mode;//1表示被动传输  0表示主动传输
}job;

//定义配置文件中参数结构体
typedef struct para_st
{
   char ip[32];
   char mode[8];
   char usr[32];
   char passwd[32];
   char path[64];
   char time[2];
}para;

//定义进行任务作业使用的消息结构体
typedef struct msg_st
{
  long int msg_type;
  job msg_job;
}job_msg;

int match(char *ptext,char *name)
{
   if(*name=='\0')
   {
      return(1);
   }
   if(*ptext=='\0')
   {
       if(*name=='*'&&*(name+1)=='\0')
         {
            return(1);
         }
       return(0);
   }
   if(*name!='*'&&*name!='?')
   {
       if(*name==*ptext)
          return  match(ptext+1,name+1);
       return(0);
   }
   if(*name=='*')
      return match(ptext+1,name)||match(ptext,name+1);
   return match(ptext+1,name+1);
}

mkdirs(char *muldir)   
{  
    int i,len;
    char str[512];
    strncpy(str, muldir, 512);
    len=strlen(str);
    for( i=0; i<len; i++ )  
    {  
        if( str[i]=='/' )  
        {  
            str[i] = '\0';  
            if( access(str,0)!=0 )  
            {  
               mkdir( str, 0775);  
            }  
            str[i]='/';  
        }  
    }  
    if( len>0 && access(str,0)!=0 )
    {  
        mkdir( str, 0775);
    }  
} 


int cpfile(char *infile,char *outfile)
{
    int c;
    FILE *in,*out;
    
    if((in=fopen(infile,"r"))==NULL)
    {            
        return(0);
    }
    
    if((out=fopen(outfile,"w"))==NULL)
    {            
        return(0);
    }
    
    while((c=fgetc(in))!=EOF)
        fputc(c,out);

    fclose(in);
    fclose(out);    
    return(1);
}

//**只能删除非.和非..和非.prcsnd的目录**//
int remove_dir(const char *dir)
{
    char dir_name[128];
    DIR *dirp;
    struct dirent *dp;
    struct stat dir_stat;

    // 参数传递进来的目录不存在，直接返回
    if ( 0 != access(dir, F_OK) ) {
        return 0;
    }

    // 获取目录属性失败，返回错误
    if ( 0 > stat(dir, &dir_stat) ) {
        perror("get directory stat error");
        return -1;
    }

    // 普通文件直接删除
    if ( S_ISREG(dir_stat.st_mode) ) 
     {
        remove(dir);
     }
    // 目录文件，递归删除目录中内容 
    else if ( S_ISDIR(dir_stat.st_mode) ) 
     {
        //printf("i am fun dir is %s\n",dir);
        dirp = opendir(dir);
        while ( (dp=readdir(dirp)) != NULL ) 
          {
            if ( (0 == strcmp(".", dp->d_name)) || (0 == strcmp("..", dp->d_name)) ) 
              {
                 continue;
              }
            sprintf(dir_name, "%s/%s", dir, dp->d_name);
            
            // 递归调用
            remove_dir(dir_name);
          }
        closedir(dirp);
        
        // 删除空目录
        rmdir(dir);
     } 
    else 
      {
        perror("unknow file type!");    
      }
    return 0;
}

int main()
{
    xmlDocPtr conf_doc;//xml文件句柄
    xmlNodePtr conf_root;//xml节点变量
    xmlChar *char_folder,*char_file,*char_para_ip,*char_para_usr,*char_para_pwd,*char_para_path;//xml类型的字符变量
    char bk_path[128];
    char unknow_path[128];
    char log_path[32];
    char *szDocName;
    xmlKeepBlanksDefault(0);//忽略xml文件中的空格

    conf_doc=xmlParseFile("../config/conf.xml");
    if(conf_doc==NULL)
     {
        fprintf(stderr,"Failed to parse conf.xml!\n");
        exit(EXIT_FAILURE);
     }
    printf("sucesful load config file!\n");
    conf_root = xmlDocGetRootElement(conf_doc);
    if (conf_root== NULL) 
     {
       fprintf(stderr,"conf.xml root is empty\n");
       exit(EXIT_FAILURE);
     }
    //xmlFreeDoc(conf_doc);   

    //打开消息队列
    int job_msgid;
    job_msgid=msgget((key_t)1235,0666 | IPC_CREAT);
    if(job_msgid==-1)
     {
        fprintf(stderr,"msgget failed with error:%d\n",errno);
        exit(EXIT_FAILURE);
     } 

    while(1)
      {
        xmlNodePtr conf_folder;//xml节点变量

        //读取头节点的全局配置信息
        strcpy(log_path,(char *)xmlGetProp(conf_root,BAD_CAST"logpath"));
        strcpy(bk_path,(char *)xmlGetProp(conf_root,BAD_CAST"bkpath"));
        strcpy(unknow_path,(char *)xmlGetProp(conf_root,BAD_CAST"unknowpath")); 
   
        //轮寻目录
        for(conf_folder=conf_root->children;conf_folder;conf_folder=conf_folder->next)
         {
            //获取节点目录的属性--获取了配置文件要轮训的目录
            char_folder=xmlGetProp(conf_folder,BAD_CAST"d");
            
            //获取这个目录要扫描多久之前的文件，这个时间由def_time确定
            char def_time[4];
            strcpy(def_time,(char *)xmlGetProp(conf_folder,BAD_CAST"scantime"));
            int scan_time=atoi(def_time);         
           
            //获取目录别名
            char flag_name[8];
            strcpy(flag_name,(char *)xmlGetProp(conf_folder,BAD_CAST"flag"));
 
            //建立移盘目录.prcsnd
            char ln_path[256];
            sprintf(ln_path,"%s/.prcsnd",(char *)char_folder);
            DIR *dp_ln_test=opendir(ln_path);
            if(NULL==dp_ln_test)
               {
                  mkdirs(ln_path);
               }
            else
               closedir(dp_ln_test);
 
            //遍历这个目录
            DIR *dp;
            if((dp=opendir((char *)char_folder))==NULL)
               {
                  fprintf(stderr,"cannot open dir:%s\n",(char *)char_folder);
                  exit(EXIT_FAILURE);
               }
            struct dirent *entry;
            struct stat statbuf; 
            while((entry=readdir(dp))!=NULL)
             {
               char fulldir[128];
               sprintf(fulldir,"%s/%s",(char *)char_folder,entry->d_name);
               lstat(fulldir,&statbuf);
               if(S_ISREG(statbuf.st_mode))
                {
                   //获得了一个文件名  filename=entry->d_name
                   char recv_filename[512];
                   sprintf(recv_filename,"%s/%s",(char *)char_folder,entry->d_name);

                   //判断这个文件是否应该处理
                   int is_head=match(entry->d_name,".*");
                   int is_end=match(entry->d_name,"*.tmp");
                   time_t t_of_now = time(NULL);
                   struct stat buf;
                   stat(recv_filename,&buf);
                   int is_time=t_of_now-buf.st_ctime;

                   //获取当前时间
                   time_t time_file;//time_t就是long int 类型
                   struct tm  *tm_file;//存储时间的结构体
                   time_file= time(NULL);
                   tm_file = localtime(&time_file);//获取了时间结构体
                   
                   //定义日志文件
                   //建立日志文件，并打开
                   char log_file[128];
                   DIR *dp_log_test=opendir(log_path);
                   if(NULL==dp_log_test)
                   {
                      mkdirs(log_path);
                   }
                   else
                       closedir(dp_log_test);
                   
                   sprintf(log_file,"%s/recv%d%02d%02d.log",log_path,tm_file->tm_year+1900,tm_file->tm_mon+1,tm_file->tm_mday);
                   FILE *fp;
                   if((fp=fopen(log_file,"at+"))==NULL)
                     {
                         fprintf(stderr,"open log faild %s\n",errno);
                         exit(EXIT_FAILURE);
                     }

                   //如果文件生成时间和现在时间差大于定义时间，而且不是以.开头的，不是以.tmp结尾的,则进行处理
                   if((is_time>scan_time)&&(is_head==0)&&(is_end==0))
                    {
                       printf("%s:%s\n",(char *)char_folder,entry->d_name);
                       //建立备份目录
                       char bk_path_time[256];
                       sprintf(bk_path_time,"%s/%d%02d%02d/%s",bk_path,tm_file->tm_year+1900,tm_file->tm_mon+1,tm_file->tm_mday,flag_name);
                       DIR *dp_bk_test=opendir(bk_path_time);
                       if(NULL==dp_bk_test)
                       {
                          mkdirs(bk_path_time);
                       }
                       else
                          closedir(dp_bk_test);
               
                      //建立unknow目录
                       char unknow_path_time[256];
                       sprintf(unknow_path_time,"%s/%d%02d%02d/%s",unknow_path,tm_file->tm_year+1900,tm_file->tm_mon+1,tm_file->tm_mday,flag_name);
                       DIR *dp_unknow_test=opendir(unknow_path_time);
                       if(NULL==dp_unknow_test)
                          {
                             mkdirs(unknow_path_time);
                          }
                       else
                          closedir(dp_unknow_test);  
                   
                       //记录当前时间
                       fprintf(fp,"%d-%02d-%02d %02d:%02d:%02d  ",tm_file->tm_year+1900,tm_file->tm_mon+1,tm_file->tm_mday,tm_file->tm_hour,tm_file->tm_min,tm_file->tm_sec);
                        
                       //记录收集的文件信息
                       fprintf(fp,"收集到文件%s:",entry->d_name);
                        
                       //先将文件备份到备份目录
                       char bk_filename[512];
                       sprintf(bk_filename,"%s/%s",bk_path_time,entry->d_name);
                       remove(bk_filename);
                       if(cpfile(recv_filename,bk_filename)== 0)//先备份到目录
                          {
                             fprintf(stderr,"备份失败:%s ---> %s\n",recv_filename,bk_filename);
                             //fprintf(fp,"move file to bkpath info:create head cp [%s] [%s] error[%s]\n",recv_filename,bk_filename, strerror(errno));
                          }
                       else
                         fprintf(fp,"备份到目录%s:",bk_filename);
                         
                         
                       xmlNodePtr conf_file;//xml文件模版节点变量
                       
                       int unknow_flag=0;
                       //遍历目录下对应的文件名模版
                       for(conf_file=conf_folder->children;conf_file;conf_file=conf_file->next)
                        {
                          //获取了该目录下，temp文件名模版
                          char_file=xmlGetProp(conf_file,BAD_CAST"Temp");
                          char temple[64];
                          strcpy(temple,(char *)char_file);
                          //看看这个temp模版对应的route部分，要发送到哪里
                          if(match(entry->d_name,temple))
                           {
                             printf("------匹配的模板为%s\n",temple);
                             unknow_flag++;
                             xmlNodePtr conf_route;//xml节点变量
                             //用来统计有多少个路由，并依次建立链接文件
                             int route_num=1;
                             for(conf_route=conf_file->children;conf_route;conf_route=conf_route->next)//遍历对应文件名模版的发送策略
                              {
                                 //把这个文件链接到.prcsnd目录下
                                 char ln_filename[512];
                                 sprintf(ln_filename,"%s/%s.%d",ln_path,entry->d_name,route_num);
                                 remove(ln_filename);
                                 if(link(recv_filename,ln_filename)!= 0)//先链接到.prcsnd目录下，以后的所有操作都是基于这个链接文件进行的
                                   {
                                     fprintf(stderr,"move file to .prcsnd info:create head link [%s] [%s] error[%s]\n",recv_filename,ln_filename, strerror(errno));
                                     continue;
                                   }
                                 fprintf(fp,"移盘到目录%s",ln_filename); 
                                                             
                                 //初始化一个配置结构体(mode ip tmp usr passwd path)
                                 para para_di;
                                 //处理当前的策略
                                 xmlNodePtr conf_para;//xml节点变量
                                 //这个策略对应的传输参数
                                 for(conf_para=conf_route->children;conf_para;conf_para=conf_para->next)
                                    {
                                       char tmp[64];
                                       strcpy(tmp,(char *)conf_para->name);
                                       //printf("para is %s\n",tmp);
                                       if(strcmp(tmp,"ip")==0)
                                          strcpy(para_di.ip,(char*)xmlNodeGetContent(conf_para));
                                       if(strcmp(tmp,"mode")==0)
                                          strcpy(para_di.mode,(char*)xmlNodeGetContent(conf_para));
                                       if(strcmp(tmp,"usr")==0)
                                          strcpy(para_di.usr,(char*)xmlNodeGetContent(conf_para));
                                       if(strcmp(tmp,"passwd")==0)
                                          strcpy(para_di.passwd,(char*)xmlNodeGetContent(conf_para));
                                       if(strcmp(tmp,"path")==0)
                                          strcpy(para_di.path,(char*)xmlNodeGetContent(conf_para));
                                       if(strcmp(tmp,"time")==0)
                                          strcpy(para_di.time,(char*)xmlNodeGetContent(conf_para));
                                    }
                                 //进行本地链接send
                                 if(strcmp(para_di.ip,"LOOP")==0)
                                   {                    
                                      char send_loop[512];
                                      sprintf(send_loop,"%s/%s",para_di.path,entry->d_name);
                                      remove(send_loop);
                                      if(cpfile(ln_filename,send_loop)== 0)//本次操作中断，进行下一个遍历
                                       {
                                         fprintf(fp,"本地分发失败:%s ---> %s\n",ln_filename,send_loop);
                                       }
                                      else
                                          fprintf(fp,"本地分发目录为%s:",send_loop);
                                      printf("----------本地分发:%s\n",send_loop);
                                      remove(ln_filename);
                                   }
                                 else
                                   {
                                      //将配置结构体转换为job结构体
                                      job_msg job_msg_di;

                                      //进行动态分发的路径编写
                                       if(strcmp(para_di.time,"0")==0)
                                          sprintf(job_msg_di.msg_job.job_url_di,"ftp://%s/%s/%s.%d.tmp",para_di.ip,para_di.path,entry->d_name,route_num);
                                       else
                                          sprintf(job_msg_di.msg_job.job_url_di,"ftp://%s/%s/%d%02d%02d/%s.%d.tmp",para_di.ip,para_di.path,tm_file->tm_year+1900,tm_file->tm_mon+1,tm_file->tm_mday,entry->d_name,route_num);

                                       //进行主被动处理
                                       if(strcmp(para_di.mode,"PASV")==0)
                                          job_msg_di.msg_job.job_ftp_mode=1;
                                       else
                                          job_msg_di.msg_job.job_ftp_mode=0;

                                      job_msg_di.msg_type=1;
                                      strcpy(job_msg_di.msg_job.job_local_di,ln_filename);
                                      sprintf(job_msg_di.msg_job.job_host_auth,"%s:%s",para_di.usr,para_di.passwd);
                                      sprintf(job_msg_di.msg_job.job_rnfr_di,"RNFR %s.%d.tmp",entry->d_name,route_num);
                                      sprintf(job_msg_di.msg_job.job_rnto_di,"RNTO %s",entry->d_name);
                                          
                                      //发送到消息队列中
                                      if(msgsnd(job_msgid,(void *)&job_msg_di,MAX_LEN,0)==-1)
                                        {
                                          fprintf(stderr,"msgsnd failed\n");
                                          exit(EXIT_FAILURE);
                                        }
                                      fprintf(fp,"发送文件到主机%s:",job_msg_di.msg_job.job_url_di);
                                      printf("----------远程分发:%s\n",job_msg_di.msg_job.job_url_di);
                                   }
                                route_num++;
                              }
                             break;
                           }
                        }
                        //备份到unknow_path目录中
                        if(unknow_flag==0)
                        {
                            char unknow_file[512];
                            sprintf(unknow_file,"%s/%s",unknow_path_time,entry->d_name);
                            remove(unknow_file);
                            if(cpfile(recv_filename,unknow_file)== 0)
                              {
                                fprintf(fp,"备份到unknowbk目录失败[%s] [%s] error[%s]\n",recv_filename,unknow_file, strerror(errno));
                              }
                            else
                               fprintf(fp,"备份到unknowbk目录%s\n",unknow_file);
                        }
                        remove(recv_filename);
                        fprintf(fp,"\n");
                    }
                   if((is_time>scan_time)&&(is_head==0)&&(is_end!=0))
                    {
                        remove(recv_filename);
                        fprintf(fp,"delete this file:%s\n",recv_filename);
                    }
                   fclose(fp);
                 }
               else if(S_ISDIR(statbuf.st_mode))
                {
                    if( \
                        (0==strcmp("..", entry->d_name)) || \
                        (0 == strcmp(".",entry->d_name)) || \
                        (0 == strcmp(".prcsnd",entry->d_name)) \
                      )
                     {
                        continue;
                     }
                    //删除这个目录
                    int rmdir_flag=remove_dir(fulldir);
                }
               else
                 {
                    printf("unknow file type!");
                 }
             }
             closedir(dp);
         }
        sleep(1);
      }
}
