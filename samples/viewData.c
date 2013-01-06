#include <stdio.h>
#include <stdlib.h>
#define DATAFILE "./data.txt"

int main (void){
    FILE *f = fopen(DATAFILE, "r");
    int ch;
    if(f == NULL){
        printf("%s%c%c\n", "Content-Type:text/html;charset=iso-8859-1", 13, 10);
        printf("<title>Failure</title>\n");
        printf("<p><em>Unable to open data file, sorry!</em></p>");
    }else{
        printf("%s%c%c\n", "Content-Type: text/html;charset=iso-8859-1", 13, 10);
        printf("<textarea cols='50' rows='30'>");
        while((ch=getc(f)) != EOF){
            putchar(ch);
        }
        fclose(f);
        printf("</textarea>");
    }
    printf("<br><a href='/formPost.html'>Go back</a>");
    return 0;
}
