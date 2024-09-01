#ifndef SEND_MAIL_H
#define SEND_MAIL_H

#include<stdio.h>
#include <string.h>

#define FROM_MAIL     "<test@gmail.com>"
#define TO_MAIL       "<testzazavrsni@gmail.com>"

static size_t payload_source(char *ptr, size_t size, size_t nmemb, void *userp);
int sendEmailAlert();

#endif