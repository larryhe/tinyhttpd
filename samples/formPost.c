#include <stdio.h>
#include <stdlib.h>
#define MAXLEN 80
#define EXTRA 5
#define MAXINPUT MAXLEN+EXTRA+2
#define DATAFILE "./data.txt"

void decode(char *src, char *last, char *dest){
    for (; src != last; src++, dest++) {
        if(*src == '+'){
            *dest = ' ';
        }else if(*src == '%'){
            int code;
            if(sscanf(src+1, "%2x", &code) != 1){
                code = '?';
            }
            *dest = code;
            src += 2;
        }else{
            *dest = *src;
        }
    }
    *dest = '\n';
    *++dest = '\0';
}

int main(void){
    char *lenstr;
    char input[MAXINPUT], data[MAXINPUT];
    long len;
    printf("%s%c%c\n", "Content-Type: text/html;charset=iso-8859-1", 13, 10);
    printf("<title>Reponse</title>\n");
    lenstr = getenv("CONTENT_LENGTH");
    if(lenstr == NULL || sscanf(lenstr, "%ld", &len) != 1 || len > MAXLEN){
        printf("<p>Error in invocation- worng form probably");
    }else{
        FILE *f;
        fgets(input, len+1, stdin);
        decode(input+EXTRA, input+len, data);
        f = fopen(DATAFILE, "a");
        if(f == NULL){
            printf("<p> Sorry, cannot store your data.");
        }else{
            fputs(data, f);
        }
        fclose(f);
        printf("<p>Thank you! the following contribution of yours has been stored:<br>%s", data);
        printf("<br><a href='/cgi-bin/viewData.cgi'>View content of Data file</a>");
    }
    return 0;
}
