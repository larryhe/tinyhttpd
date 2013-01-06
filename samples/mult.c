#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    char *data;
    long m,n;
    printf("%s%c%c\n", "Content-Type:text/html;charset=iso-8859-1",13,10);
    printf("<TITLE>Multiplication results</TITLE>\n");
    printf("<H3>Multiplication results</H3>\n");
    data = getenv("QUERY_STRING");
    if(data == NULL)
          printf("<P>Error! Error in passing data from form to script.");
    else if(sscanf(data,"m=%ld&n=%ld",&m,&n)!=2)
          printf("<P>Error! Invalid data. Data must be numeric.");
    else
          printf("<P>The product of %ld and %ld is %ld.",m,n,m*n);
    return 0;
}
