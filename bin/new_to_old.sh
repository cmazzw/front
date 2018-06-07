#!/bin/bash

cp ../config/conf.jd.xml ../config/conf.xml
sed -i "s/xxxxx/"$1"/g" ../config/conf.xml
sed -i "s/yyyyy/"$2"/g" ../config/conf.xml

echo "开始切换"
echo "......"

ps -ef|grep f_|grep -v 'grep'|awk '{print $2}' |xargs kill -9

sleep 2

nohup ./f_recv 1> /dev/null 2> /home/filecom/log/recverr.log &
nohup ./f_send 1> /dev/null 2> /home/filecom/log/senderr.log &
nohup ./f_send 1> /dev/null 2> /home/filecom/log/senderr.log &
nohup ./f_send 1> /dev/null 2> /home/filecom/log/senderr.log &
nohup ./f_send 1> /dev/null 2> /home/filecom/log/senderr.log &

echo "您辛苦了！"$1"切换成功！"

exit