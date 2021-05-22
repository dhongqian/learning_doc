#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

void read_cb(struct bufferevent *bev, void *arg)
{
  char buf[1024] = {0}; 
  bufferevent_read(bev, buf, sizeof(buf));
  printf("Server say: %s\n", buf);
}

void write_cb(struct bufferevent *bev, void *arg)
{
  printf("����д�������Ļص�����...���ѷ���\n"); 
}

void event_cb(struct bufferevent *bev, short events, void *arg)
{
  if (events & BEV_EVENT_EOF)
  {
    printf("connection closed\n");  
  }
  else if(events & BEV_EVENT_ERROR)   
  {
    printf("some other error\n");
  }
  else if(events & BEV_EVENT_CONNECTED)
  {
    printf("������������\n");
    return;
  }

  bufferevent_free(bev);
  printf("free bufferevent...\n");
}

void send_cb(evutil_socket_t fd, short what, void *arg)
{
  char buf[1024] = {0}; 
  struct bufferevent* bev = (struct bufferevent*)arg;
  // printf("������Ҫ���͵�����: \n");
  ::read(fd, buf, sizeof(buf));
  bufferevent_write(bev, buf, strlen(buf)+1);
}


int main(int argc, const char* argv[])
{
  struct event_base* base;
  base = event_base_new();


  struct bufferevent* bev;
  bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);

  // ���ӷ�����
  struct sockaddr_in serv;
  memset(&serv, 0, sizeof(serv));
  serv.sin_family = AF_INET;
  serv.sin_port = htons(9876);
  evutil_inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr.s_addr);
  bufferevent_socket_connect(bev, (struct sockaddr*)&serv, sizeof(serv));

  // ���ûص�
  bufferevent_setcb(bev, read_cb, write_cb, event_cb, NULL);
  bufferevent_enable(bev, EV_READ | EV_PERSIST);

  // ����һ���¼�
  struct event* ev = event_new(base, STDIN_FILENO, 
    EV_READ | EV_PERSIST, 
    send_cb, bev);
  event_add(ev, NULL);

  event_base_dispatch(base);

  event_base_free(base);

  return 0;
}
