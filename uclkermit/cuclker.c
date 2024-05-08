/* File uclk15.c on ucl u44d   -   UCL Kermit main source file;
                    Chris Kennington.       30th September 1985         */


/*  This is a remote-only Kermit for Berkeley Unix 2.8 (11/44),
          produced at UCL by surgery on the "demonstration" Kermit,
          plus enhancements.
    The program should run unmodified in any unix-like environment;
          for use in any other environment in which an ASCII OS supports
          a VDU terminal on an RS232 port, there should be no need to
          modify any routines in this file.  All possibly machine-dependent
          routines have been isolated in file "uclkmd.c"; these should
          be checked before compilation.
    8th-bit prefixing is specified by a "8" flag on the command-line;
          "7" forces 7-bit, "i" forces 8-bit image.  If no such flag, then
          7-bit is assumed.  Repeat-prefixing is always potentially on.
    In 7-bit mode only: LF is converted to CR/LF in transmission
          and CR, LF, CR/LF or LF/CR to LF on receipt; this code is
          in the machine-dependent routines.
             Chris Kennington.       UCL.    9th May 1985.        */

/*  The modification history of this program is in file uclkmods.dox    */



/* Identification string:                                       */
char  ident[] ="UCL Remote-only Kermit,  V15B,  March 1986.\n";


#include <stdio.h>          /* Standard UNIX definitions */
#include <ctype.h>          /* isascii definitions       */


/* MAX & MIN values accepted                            */
#define MAXPACKSIZ  94      /* Maximum packet size */
#define MAXTIM      60      /* Maximum timeout interval */
#define MAXTRY      10      /* Times to retry a packet */
#define MINTIM      2       /* Minumum timeout interval */

/* My values to send to other end                       */
#define MYEOL       '\n'    /* End-Of-Line character I need */
#define MYPAD       0       /* Number of padding characters I will need */
#define MYPCHAR     0       /* Padding character I need (NULL) */
#define MYQUOTE     '#'     /* Quote character I will use */
#define MYTIME      10      /* Seconds after which I should be timed out */

/* Boolean constants                                    */
#define TRUE        -1
#define FALSE       0

/* ASCII Chars                                          */
#define LF          10
#define CR          13      /* ASCII Carriage Return */
#define DEL         127     /* Delete (rubout) */
#define ESC         0x1b    /* real escape char        */
#define SOH         1       /* Start of header */
#define SP          32      /* ASCII space */

/* Macro Definitions */

/*
 * tochar: converts a control character to a printable one by adding a space.
 * unchar: undoes tochar.
 * ctl:    converts between control characters and printable characters by
 *         toggling the control bit (ie. ^A becomes A and A becomes ^A).
 */
#define tochar(ch)  ((ch) + ' ')
#define unchar(ch)  ((ch) - ' ')
#define ctl(ch)     ((ch) ^ 64 )

#define forever     while(1)

/* procedures passed as arguments etc.:-                                */
extern  char cread();
extern  char rinit(), rfile(), rdata();     /* receive procedures       */
extern  char sinit(), sfile(), sdata(), seof(), sbreak(); /* send procs */
extern  int  outfc(), outfile();

/* procedures in system-dependent file uclkmd.c:-                       */
extern  char   ascout(), ascedit(), filerr(), nextin();
extern  int    flushinput(), cooktty(), rawtty(), timoset(), unbuffer();



/* Global Variables */

int
          aflg, rflg, sflg,   /* flags for AUTO, RECEIVE, SEND */
          debug,              /* indicates level of debugging output (0=none) */
          filnamcnv,          /* -1 means do file name case conversions */
          filecount,          /* Number of files left to send */
          image, oimage,      /* 0 = 7-bit, 1 = 8-bit mode, 2 = prefixing */
          n,                  /* Packet number */
          oldt,               /* previous char in encode()        */
          numtry,             /* Times this packet retried */
          oldtry,             /* Times previous packet retried */
          pad,                /* How much padding to send */
          qu8,
          rpt,
          rptflg,
          sz,
          size,               /* Size of present data */
          spsiz,              /* Maximum send packet size */
          timint = 10;        /* Timeout for foreign host on sends */

char
          c,                  /* dummy char               */
          *dname,             /* name of debug file UCL   */
          *dt,                /* data-block code/decode   */
          eol = '\n',         /* End-Of-Line character to send */
          **filelist,         /* List of files to be sent */
          *filenames[11],
          *filnam,            /* Current file name */
          getfiles[MAXPACKSIZ],
          iflg = 0,           /* 8th-bit-mode set */
          packet[MAXPACKSIZ], /* Packet buffer */
          padchar,            /* Padding character to send */
          quote = '#',        /* Quote character in incoming data */
          recpkt[MAXPACKSIZ], /* Receive packet buffer */
          state,              /* Present state of the automaton */
          timflag,            /* timeout flag UCL         */
          type;               /* received-packet type     */

char  amauto[]   = "%sI am in automatic-server mode!";
char  badpack[]  = "%sYour Kermit sent invalid packet, type \"%c\"!";
char  crlf[]     = "\r\n";
char  *eighths[] = {"7-bit character", "binary-image-mode", "8th-bit prefixed"};
char  goodbye[]  = "%sReturning to Unix; goodbye.";
char  *logicval[]= {"OFF", "ON"};
char  noname[]   = "%sNo valid names in GET request";
char  null[]     = "";       /* null string              */
char  onlyg[]    = "%sOnly BYE, LOGOUT, FINISH commands implemented";
char  prompt[]   = "KmUCL: ";

FILE    *fp,                /* current disk file */
          *dfp;               /* debug file UCL           */



/*
 *  m a i n
 *  Main routine - parse command and options, set up the
 *  tty lines, and dispatch to the appropriate routine.
 */

main(argc,argv)
int argc;                           /* Character pointers to and count of */
char **argv;                            /* command line arguments */
{
    char *cp, **argv1;
    static char first = 0;
    int  argc1;

    if (first != 0) {
          printmsg("Program fault - reentered.\n");
          exit(1);
    }
    else
          ++first;
    unbuffer();                         /* unbuffer output */
    printf(ident);
    aflg = sflg = rflg = 0;             /* Turn off all parse flags */
    cp = *++argv;
    argv1 = argv;                       /* remember argument pointers */
    argv++;
    argc1 = --argc;
    --argc;                             /* pointers to args */
/*  Initialize these values and hope the first packet will get across OK */
    eol = CR;                           /* EOL for outgoing packets */
    quote = '#';                        /* Standard control-quote char "#" */
    pad = 0;                            /* No padding */
    padchar = NULL;                     /* Use null if any padding wanted */
    spsiz = MAXPACKSIZ;
    qu8 = 0;
    image = -1;
    rptflg = TRUE;                      /* try for repeating    */
    filnamcnv = TRUE;                   /* conversion for UNIX systems */

    dfp = 0;
    dname = 0;                          /* clear debug file ptrs        */

    if (argc >= 0) while ((*cp) != NULL) switch (*cp++) {
  /* If command-line, parse characters in first arg.            */

              case 'a':
                    aflg++;  break;         /* A = Auto command     */

              case 'f':
                    filnamcnv = FALSE;      /* F = don't do case conversion */
                    break;                  /*     on filenames */

              case 'h':
                    debug = 0;              /* there won't be a file yet */
                    help();                 /* H = help messages    */

              case 'i':                   /* I = Image (8-bit) mode */
                    image = 1;
                    ++iflg;
                    break;

              case 'r':
                    rflg++; break;          /* R = Receive command */

              case 's':
                    sflg++;  break;         /* S = Send command */

              case '7':                   /* 7-bit transfer               */
                    image = 0;
                    ++iflg;
                    break;

              case '8':                   /* 8-bit prefixed               */
                    image = 2;
                    ++iflg;
                    break;

              case '-':                   /* - flag (ignored)             */
                    break;

              default:
                    --cp;
                    printmsg("Invalid char %c in command-line.",*cp);
                    usage();
          }
    if (image == -1)
          image = 0;              /* default is 7-bit             */
    oimage = image;             /* remember setting             */

/* Flags parsed, check for debug filename                       */
    cp = *argv;
    if (*cp == '-') {
          if (*++cp != 'D') {     /* invalid debug filename       */
              printmsg("Debug filename <%s> must start with \"D\".",*--cp);
              usage();
          }
          while (*cp == 'D') {    /* count the Ds                 */
              ++debug;
              ++cp;
          }
          dname = cp;             /* filename starts after Ds     */
          dfp = fopen(dname,"a");
          ++argv;
          --argc;
    }

    if ((c = aflg+sflg+rflg) > 1) {         /* Only one command allowed */
          printmsg("One only of Auto OR Receive OR Send!");
          usage();
    }
    if (iflg > 1) {
          printmsg("One only of \"i\" or \"7\" !");
          usage();
    }
    printmsg("%s transfer requested;",eighths[image]);
    if (image != 0)
          printf("        CR <=> LF translation will NOT be done.\n");

    if (c == 0) {               /* no action-flag       */
          printf(crlf);
          printmsg("No action on command-line; Automatic mode assumed;\n  For help use \"kermit  h\".");
          aflg = 1;
    }

/* Put the tty into the correct mode */
    rawtty();

/* All set up, now execute the command that was given. */

    if (debug) {
          if (dfp != 0) {
              printmsg("Debugging level = %d into file \"%s\";",debug,dname);
              fprintf(dfp,"\n\n******** UCL Kermit Debug File ** next run starts here  **********\n");
              fprintf(dfp,ident);
              fprintf(dfp,"\nDebugging level = %d into file \"%s\";\nCommand-line: <",debug,dname);
              while (argc1-- > 0)
                    fprintf(dfp,"  %s",*argv1++);
              fprintf(dfp,"  >;");
              if (aflg) fprintf(dfp,"\nAuto Command\n");
              if (sflg) fprintf(dfp,"\nSend Command\n");
              if (rflg) fprintf(dfp,"\nReceive Command\n");
          }
          else {
              printf("No valid debug file, debug switched off.\r\n");
              debug = 0;
          }
    }

    if (sflg)                           /* Send command */
    {
          if (argc--) {
              printmsg("Send Command; set your Kermit to receive files\n");
              filnam = *argv++;   /* Get file to send */
          }
          else {
              cooktty();                  /* restore tty to normal  */
              printmsg("Send, but no filename given;\n");
              usage();                    /* and give error */
          }
          fp = NULL;                      /* Indicate no file open yet */
          filelist = argv;                /* Set up the rest of the file list */
          filecount = argc;               /* Number of files left to send */
          printmsg("I'll give you 10 seconds to get ready ...");
          timoset(10);
          while (timflag != 0)
              c = cread();                /* hang about           */
          if (sendsw() == FALSE)          /* Send the file(s) */
              printmsg("Send failed.");   /* Report failure */
          else                            /*  or */
              printmsg("done.");          /* success */
    }

    else if (rflg) {                         /* Receive command */
          printmsg("Receive Command; set your Kermit to Send files\n");
          printmsg("I'll give you 5 seconds to get ready ...");
          timoset(5);
          while (timflag != 0)
              c = cread();                /* hang about           */
          timint = 20;
          if (recsw() == FALSE)           /* Receive the file(s) */
              printmsg("Receive failed.");
          else                            /* Report failure */
              printmsg("done.");          /* or success */
    }

    else while (aflg) {         /* automatic (sub-server) mode  */
          printmsg("Auto Command; set your Kermit to Send or Get files;");
printf("\tto cancel, use \"LOGOUT\", \"BYE\" or \"FINISH\" command,\r\n");
          printf("\t(or send ESCAPE-C at any time).\r\n");
          if (autosw() ==  TRUE)
              printmsg("Auto Send/Get complete");
          else
              printmsg("Auto-mode cancelled");
    }
    closeall();
    cooktty();
    exit(0);
}                       /* End main()                   */



autosw()                /* state switcher for automatic mode     */
{
    int   len, num;
    char  c, *wild;

    forever {
          timint = 40;           /* slow NAKs at first           */
          if (debug)
              fprintf(dfp,"\nAutosw() waiting:  8th-bit setting now %d, char is %c; reptflag %s.\n",image,qu8,logicval[-rptflg]);
          n = numtry = 0;
          switch ( (type = rpack(&len,&num,recpkt)) ) {
  /* decipher request from micro                                */

            case 'S':             /* send-init                    */
  /* receive file(s) from local Kermit                          */
              state = 'F';
              rpar(recpkt,len);
              len = spar(packet);
              spack('Y',n,len,packet);     /* ack parameters   */
              n = (n+1)%64;
              while (state != 'C') {      /* until EoF            */
                    if (debug == 1) fprintf(dfp," autosw state %c\n",state);
                    switch(state) {
                      case 'F':  state = rfile();  break;
                      case 'D':  state = rdata();  break;
                      case 'A':  state = 'C';  break;
                      case 'C':  break;
              }   }                       /* end switch, while    */
              image = oimage;
              break;                      /* end of reception     */

            case 'R':             /* get command                  */
  /* send file(s) to micro Kermit                               */
if (debug) fprintf(dfp,"\n  getting files <%s>, ", recpkt);
              movemem(recpkt,getfiles,len); /* file name(s)       */
              getfiles[len] = 0;
              wild = getfiles;
              while ( (c = (*wild++)&0x7f) != 0 )
                    if ( (c == '*') || (c == '?') ) {
                        error("Cannot accept wildcard <%c> in \"GET\"",c);
                        break;      /* no wildcard processing       */
                    }
              filecount = decol8(getfiles,filenames,10);
              filelist = filenames;
if (debug > 1) {
    fprintf(dfp," %d files, ",filecount);
    while (*filelist != 0)
          fprintf(dfp,"<%s> ",*filelist++);
    filelist = filenames;
}
              if (filecount--) {          /* if any valid names   */
                    filnam = *filelist++;
                    fp = NULL;
                    if (sendsw() == FALSE)          /* Send the file(s) */
                        printmsg("Send failed.");   /* Report failure */
                    else
                        printmsg("Files sent; still in auto-mode.");
              }
              else                        /* if no names          */
                    error(noname,prompt);
              image = oimage;
              break;

            case 'G':             /* generic command              */
  /* only "BYE" and "LOGOUT" are acceptable                     */
              if ( ( (c = *recpkt&0x5f) == 'F' ) || (c == 'L') ) {
                    error(goodbye,prompt);
                    aflg = 0;
                    return(TRUE);
              }
              else
                    error(onlyg,prompt);
              break;

            case 'I':             /* info-exchange packet         */
              rpar(recpkt,len);
              len = spar(packet);
              spack('Y',n,len,packet);      /* ack the parameters   */
              break;

            case 'E':             /* error packet                 */
              prerrpkt(recpkt);
              aflg = 0;
              error(goodbye,prompt);
              return(FALSE);

            case 'C':             /* OS command                   */
              error(onlyg,prompt);
              break;

            case 'N':             /* NAK - out of phase           */
              error(amauto,prompt);
              break;

            case FALSE:           /* timeout - micro not sending  */
              printf(crlf);
              printmsg("Please enter \"SEND\", \"GET\" or \"BYE\" command to micro Kermit,");
              printf("\t(or send ESCAPE-C to cancel UCL Kermit) ...\n");
              spack('N',0,0,null);
              break;

            default:              /* bad packet                   */
              error(badpack,prompt,type);
              return(FALSE);

    }   }               /* end switch, forever                  */
}               /* End autosw()                                 */



bufill(buffer)
/*  Get a bufferful of data from the file that's being sent     */
char *buffer;
{
    int t;
    static  int  softeof= FALSE;

    if (softeof == TRUE) {
          softeof = FALSE;
          return(EOF);
    }
    rpt = sz = 0;                       /* Inits for encode()   */
    dt = buffer;
    oldt = -2;                          /* impossible last char */
    while((t = getc(fp)) != EOF) {      /* next character       */
          t = ascedit(t);                 /*  edited if necessary */
          encode(t);                      /*   to buffer          */
          if (sz >= spsiz-6)              /* Check length         */
               return(sz);
    }
  /* reach here on (hard) EOF or error                          */
    if (sz==0)
       return(EOF);
    else {
          softeof = TRUE;                 /* EOF next time        */
          return(sz);
    }
}                       /* end of bufill()                      */


closeall()              /* close both files             */
{
    if (fp != NULL)
          fclose(fp);
    if (dfp != NULL) {
          fprintf(dfp,"\n\n************************* End of Run ******************************\n\n");
          fclose(dfp);
    }
}                       /* end of closeall()            */



char  cread()           /* get next char from line      */
/*  UCL Input Routine: handle escapes as well,
          return SOH & printables, eat all other controls. */
{
    char  c, t;
    int   ex;

    ex =  0;
    while (ex == 0) {
          t = nextin();           /* get next char        */
          if (timflag == 0)
              return(0);          /* timeout occured      */
          c = t&0x7f;
          if (image != 1)
              t = c;
          if ( (c == CR) || (c == LF) || (c == SOH) || (c > 0x1f) )
              return(t);
/* left only with invalid controls */
          if (c == ESC) {         /* process escape       */
              printf("\nKmUCL-ESC");
Repeat:                         /* for ignoring "-"     */
              read (0, &t, 1);
              putchar(t);
              c = t&0x5f;
              switch(c) {
                case  SOH:
                    return(t);
                case  'C':
                    cooktty();
                    printf("\nUCL Kermit exitting by ESC-C from local station\n");
                    closeall();
                    exit(1);
                case 'H':
                    printf("\n\rUCL Kermit is alive and well ....\n\r");
                    break;
                case 0x1f:                /* ?            */
                case 0x0f:                /* /            */
                    cooktty();
                    help1();
                    rawtty();
                    break;
                case '-':
                    goto Repeat;
                default:
                    printf("???");
                    break;
    }   }   }
    return(0);
}                       /* End of cread()               */




decode(buf,len)              /* packet decoding procedure    */
/* Called with string to be decoded; Returns 0 or error-code. */
char *buf;
int  len;
{
    char  a, a7, b8, *end, rep;
    int   flg8=0, error=0, r;

    if (image == 2)
          flg8 = -1;

    end = buf + len;
    while (buf < end) {
          a = *buf++;
          if ( rptflg && (a == '~') ) {   /* got a repeat prefix? */
                    rep = unchar(*buf++);   /* Yes, get the repeat count, */
                    a = *buf++;             /* and get the prefixed character. */
          }
          else
              rep = 1;
          b8 = 0;                         /* Check high order "8th" bit */
          if ( flg8 && (a == qu8) ) {
                    b8 = 0200;
                    a = *buf++;             /* and get the prefixed character. */
              }
          if (a == quote) {               /* If control prefix, */
              a  = *buf++;                /* get its operand. */
              a7 = a & 0177;              /* Only look at low 7 bits. */
              if ((a7 >= 0100 && a7 <= 0137) || a7 == '?') /* Uncontrollify */
              a = ctl(a);                 /* if in control range. */
          }
          a |= b8;                        /* OR in the 8th bit */

          while (rep-- > 0) {
              if (image == 0)            /*  if 7-bit            */
                    r = ascout(a);
              else                        /*  prefixing / image   */
                    r = putc(a,fp);
              if (r == EOF)               /* if error             */
                    error |= filerr();
    }   }
    return(error);
}                       /* end of decode()                      */



decol8(line,arr,num)      /* decollate command-line       */
/* Splits up line into sections delimited by controls or blanks,
     zeros all such chars & places start-addresses into array,
     up to maximum of num entries; zeros rest of entries and
     returns count of valid entries.                    */
char  *line, *arr[], num;
{
    char  c, count, *start;
    int   i, j;

    j = count = 0;
    start = line;
    for (i=0; i<80, j<num; ++i) {
          if ( (c = line[i]) <= SP) {     /* terminator   */
              line[i] = 0;
              if (count > 0) {
                    arr[j++] = start;
                    count = 0;
              }
              if (c == 0)
                    break;          /* out of for           */
          }
          else if (count++ == 0)  /* printable            */
              start = &line[i];   /* start next parm      */
    }                           /* end else, for        */
    line[i] = 0;                /* terminate last parm  */
    i = j;                      /* number of parms      */
    while (j < num)
          arr[j++] = 0;           /* clear garbage        */
    return(i);
}                       /* End of decol8()              */



encode(a)
/* encode single character into packet for transmission */
int a;                          /* char to be encoded           */
{
    int a7;                             /* Low order 7 bits     */
    int b8;                             /* 8th bit of character */
    int flg8 = 0;
    static  int  oldsz;

    if (image == 2)
          flg8 = -1;

    if (rptflg) {               /* repeat-count processing      */
          if (a == oldt) {                /* char is same         */
/* This algorithm is simple but relatively inefficient; it stores
          the repeat flag, count and character each time around so
          that when the run is broken the buffer is valid; also it
          treats a pair as a run, which requires 3 bytes not 2 unless
          the pair is control- or 8bit-prefixed; but it does not
          require lookahead logic from the data-read.             */
              sz = oldsz;                 /* wind back pointer    */
              dt[sz++] = '~';             /*  store prefix        */
              dt[sz++] = tochar(++rpt);   /*   & count            */
              if (rpt > 93)               /* force new start      */
                    oldt = -2;              /* impossible value     */
          }
          else {                          /* not run, or end      */
              rpt = 1;
              oldt = a;                   /* save char            */
              oldsz = sz;
    }   }

    a7 = a & 0177;                      /* Isolate ASCII part */
    b8 = a & 0200;                      /* and 8th (parity) bit. */

    if (flg8 && b8) {                   /* Do 8th bit prefix if necessary. */
          dt[sz++] = qu8;
          a = a7;
    }
    if ((a7 < SP) || (a7==DEL)) {      /* Do control prefix if necessary */
          dt[sz++] = MYQUOTE;
          a = ctl(a);
    }
    if (a7 == MYQUOTE)                  /* Prefix the control prefix */
          dt[sz++] = MYQUOTE;
    else if (rptflg && (a7 == '~'))     /* If it's the repeat prefix, */
          dt[sz++] = MYQUOTE;             /* quote it if doing repeat counts. */
    else if (flg8 && (a7 == qu8))               /* Prefix the 8th bit prefix */
          dt[sz++] = MYQUOTE;             /* if doing 8th-bit prefixes */

    dt[sz++] = a;                       /* Finally, insert the character */
    dt[sz] = '\0';                      /* itself, and mark the end. */
    return;
}                       /* end of encode()              */



/*VARARGS1*/
error(fmt, a1, a2, a3, a4, a5)
/*  Remote; send an error packet with the message.
 *    and if debug log in debug file                    */
char *fmt;
{
    char msg[100];
    int len;

    sprintf(msg,fmt,a1,a2,a3,a4,a5); /* Make it a string */
    len = strlen(msg);
    spack('E',n,len,msg);           /* Send the error packet */
    if (debug) fprintf(dfp,msg);
    return;
}                       /* End of error()               */



gnxtfl()                /*  Get next file in a file group       */
{
    if (debug) fprintf(dfp,"\n   gnxtfl: filelist = \"%s\"",*filelist);
    filnam = *(filelist++);
    if (filecount-- == 0)
          return FALSE; /* If no more, fail */
    else
          return TRUE;                   /* else succeed */
}                       /* End gnxtfl()                 */




help()                  /* UCL help routine             */
{
    printf("\nSummary of UCL Remote Kermit:\n");
    help1();
    printf("\t\t\t\t hit CR for more ");
    timoset(30);
    while (timflag != 0)
          if (cread() != 0)
              break;
    printf("\nBasic syntax of the \"kermit\" command is:\n");
    usage();                    /* exits                */
}                       /* End of help()                */



help1()
{
    printf("\nThis is a remote receive-or-send-or-auto Kermit; normally called\n");
    printf("  with a system command-line, failing which it enters automatic mode.\n");
    printf("Image-mode transfers may be selected by \"i\"-flag, 8-bit-prefixed transfers\n");
    printf("  by \"8\"-flag, otherwise 7-bit data only will be transferred;\n");
    printf("  mapping between LFs and CR/LF pairs is only done during 7-bit transfers.\n");
    printf("Repeat-prefixing is always available; there is NO Connect facility.\n");
    printf("Automatic mode supports the Server-Kermit commands \"SEND\", \"GET\"\n");
    printf("  and \"LOGOUT/BYE/QUIT\".\n");
    printf("If your transfer breaks down, go into connect-mode and enter\n");
    printf("  ESCAPE-C, which will cause UCL Kermit to quit gracefully;\n");
    printf("  alternatively enter ESCAPE-H, which will elicit a reassurance.\n");
    printf("If debug is requested by \"-D\" flag(s) on second parameter, then\n");
    printf("  debug information is written into a file whose name is \n");
    printf("  the rest of the second parameter (after last \"D\"); up to three\n");
    printf("  \"D\" flags may be entered (but only one \"-\"):\n");
    printf("Debug = 1 gives basic trace of automaton states + error-messages,\n");
    printf("      = 2 logs packets as sent/received,\n");
    printf("      = 3 also logs all chars as read from line, in hex.\n");
    return;
}                               /* End of help1()                       */



movemem(from,to,count)          /* shift block of data                  */
/* moves chars starting at bottom of block                              */
char    *from, *to;
int     count;
{
    while (count-- > 0)
          *to++ = *from++;
    return;
}                               /* End of movemem()                     */



prerrpkt(msg)  /* Print contents of error packet received from remote host. */
char *msg;
{
    if (debug) {
          fprintf(dfp,"\nKmUCL Aborting with following message from local Kermit:");
          msg[50] = 0;
          fprintf(dfp,"\n  \"%s\"",msg);
    }
    return;
}                       /* End prerrpkt()               */




/*VARARGS1*/
printmsg(fmt, a1, a2, a3, a4, a5)
/*  Print message on standard output ;
 *       message will only be received if remote is in connect-mode. */
char *fmt;
{
          printf("KmUCL: ");
          printf(fmt,a1,a2,a3,a4,a5);
          printf("\n\r");
          return;
}                       /* End of printmsg()                    */




char rdata()            /*  Receive Data                */
{
    int num, len;                       /* Packet number, length */

    if (debug > 2)    printf(" ** rdata() ");
    if (numtry++ > MAXTRY) return('A'); /* "abort" if too many tries */

    switch(rpack(&len,&num,packet))     /* Get packet */
    {
          case 'D':                       /* Got Data packet */
              if (num != n)               /* Right packet? */
              {                           /* No */
                    if (oldtry++ > MAXTRY)
                        return('A');        /* If too many tries, abort */
                    if (num == ((n==0) ? 63:n-1)) { /* duplicate?   */
                        spack('Y',num,0,null); /* Yes, re-ACK it    */
                        numtry = 0;         /* Reset try counter    */
                        return(state);      /* Don't write out data! */
                    }
                    else return('A');       /* sorry, wrong number */
              }
              /* Got data with right packet number */
              if ( (num = decode(packet,len)) != 0 ) {
                    error("Trouble writing file, OS code %xx",num);
                    return('A');
              }
              spack('Y',n,0,null);           /* Acknowledge the packet */
              n = (n+1)%64;               /* Bump packet number, mod 64 */
              oldtry = numtry;            /* Reset the try counters */
              numtry = 0;                 /* ... */
              return('D');                /* Remain in data state */

          case 'F':                       /* Got a File Header */
              if (oldtry++ > MAXTRY)
                    return('A');            /* If too many tries, "abort" */
              if (num == ((n==0) ? 63:n-1)) /* Else check packet number */
              {                           /* It was the previous one */
                    spack('Y',num,0,null);     /* ACK it again */
                    numtry = 0;             /* Reset try counter */
                    return(state);          /* Stay in Data state */
              }
              else return('A');           /* Not previous packet, "abort" */

          case 'Z':                       /* End-Of-File */
              if (num != n) return('A');  /* Must have right packet number */
              spack('Y',n,0,null);           /* OK, ACK it. */
              fclose(fp);                 /* Close the file */
              fp = NULL;
              if (debug) {
                    if ( (len != 0) && (*packet == 'D') )
                        fprintf(dfp,"\nFile <%s> truncated by local Kermit\n",filnam);
                    else
                        fprintf(dfp,"\nFile <%s> received OK\m",filnam);
              }
              if ( (len != 0) && (*packet == 'D') && (image != 1) )
                    fprintf(fp,"\n\n*** Local Kermit Truncated this File <%s> ***\n", filnam);
              n = (n+1)%64;               /* Bump packet number */
              return('F');                /* Go back to Receive File state */

          case 'E':                       /* Error packet received */
              prerrpkt(packet);           /* Print it out and */
              return('A');                /* abort */

          case FALSE:                     /* Didn't get packet */
              spack('N',n,0,null);           /* Return a NAK */
              return(state);              /* Keep trying */

          default:
              error(badpack,prompt,type);
              return('A');       /* Some other packet, "abort" */
    }
}                       /* End of rdata()               */




recsw()         /* state table switcher for receiving files.    */
{
    if (debug) fprintf(dfp,"Ready to receive file\n");
    if (debug > 2)    printf(" ** recsw() ");
    state = 'R';                        /* Receive-Init is the start state */
    n = 0;                              /* Initialize message number */
    numtry = 0;                         /* Say no tries yet */

    forever {
          if (debug == 1) fprintf(dfp," recsw state: %c\n",state);
          switch(state)                   /* Do until done */
          {
              case 'R':   state = rinit(); break; /* Receive-Init */
              case 'F':   state = rfile(); break; /* Receive-File */
              case 'D':   state = rdata(); break; /* Receive-Data */
              case 'C':   return(TRUE);           /* Complete state */
              case 'A':   return(FALSE);          /* "Abort" state */
          }
    }
}                       /* End recsw()                          */



char rfile()            /*  Receive File Header                 */
{
    int num, len;                       /* Packet number, length */
    char filnam1[50];                   /* Holds the converted file name */

    if (debug > 2)    printf(" ** rfile() ");
    if (numtry++ > MAXTRY) return('A'); /* "abort" if too many tries */

    switch(rpack(&len,&num,packet))     /* Get a packet */
    {
          case 'S':                       /* Send-Init, maybe our ACK lost */
              if (oldtry++ > MAXTRY) return('A'); /* If too many tries "abort" */
              if (num == ((n==0) ? 63:n-1)) { /* Previous packet, mod 64? */
                    len = spar(packet);           /* our Send-Init parameters */
                    spack('Y',num,len,packet);
                    numtry = 0;             /* Reset try counter */
                    return(state);          /* Stay in this state */
              }
              else return('A');           /* Not previous packet, "abort" */

          case 'Z':                       /* End-Of-File */
              if (oldtry++ > MAXTRY) return('A');
              if (num == ((n==0) ? 63:n-1)) /* Previous packet, mod 64? */
              {                           /* Yes, ACK it again. */
                    spack('Y',num,0,null);
                    numtry = 0;
                    return(state);          /* Stay in this state */
              }
              else return('A');           /* Not previous packet, "abort" */

          case 'F':                       /* File Header (just what we want) */
              if (num != n) return('A');  /* The packet number must be right */
              packet[49] = 0;             /* ensure string closed */
              strcpy(filnam1, packet);    /* Copy the file name */

              if (filnamcnv) {            /* Convert upper case to lower */
                    for (filnam=filnam1; *filnam != '\0'; filnam++)
                        if (*filnam >= 'A' && *filnam <= 'Z')
                              *filnam |= 040;
                    filnam = filnam1;
              }

              if (debug) fprintf(dfp,"\nOpening <%s> for receiving.",filnam1);
              if ((fp=fopen(filnam1,"w")) == NULL) {
                    error("%sCannot create <%s>",prompt,filnam1);
                    return('A');
              }
              else                        /* OK, give message */
                    if (debug) fprintf(dfp,"\nReceiving %s as %s",packet,filnam1);

              spack('Y',n,0,null);           /* Acknowledge the file header */
              oldtry = numtry;            /* Reset try counters */
              numtry = 0;                 /* ... */
              n = (n+1)%64;               /* Bump packet number, mod 64 */
              return('D');                /* Switch to Data state */

          case 'B':                       /* Break transmission (EOT) */
              if (num != n) return ('A'); /* Need right packet number here */
              spack('Y',n,0,null);           /* Say OK */
              if (debug) fprintf(dfp,"All files received\n");
              return('C');                /* Go to complete state */

          case 'E':                       /* Error packet received */
              prerrpkt(packet);           /* Print it out and */
              return('A');                /* abort */

          case FALSE:                     /* Didn't get packet */
              spack('N',n,0,null);           /* Return a NAK */
              return(state);              /* Keep trying */

          default:
              error(badpack,prompt,type);
              return ('A');       /* Some other packet, "abort" */
    }
}                       /* End rfile()                  */



char rinit()            /*  Receive Initialization              */
{
    int len, num;                       /* Packet length, number */

    if (debug > 2)    printf(" ** rinit() ");
    if (numtry++ > MAXTRY) return('A'); /* If too many tries, "abort" */

    switch(rpack(&len,&num,packet))     /* Get a packet */
    {
          case 'S':                       /* Send-Init */
              rpar(packet,len);
              len = spar(packet);
              spack('Y',n,len,packet);      /* ACK with my parameters */
              oldtry = numtry;            /* Save old try count */
              numtry = 0;                 /* Start a new counter */
              n = (n+1)%64;               /* Bump packet number, mod 64 */
              return('F');                /* Enter File-Receive state */

          case 'E':                       /* Error packet received */
              prerrpkt(packet);           /* Print it out and */
              return('A');                /* abort */

          case 'I':                       /* Init-parameters      */
              rpar(packet,len);
              len = spar(packet);
              spack('Y',n,len,packet); /* ack with our parameters */
              n = (n+1)%64;               /* Bump packet number, mod 64 */
              return(state);              /*  & continue          */

          case FALSE:                     /* Didn't get packet */
              spack('N',n,0,null);           /* Return a NAK */
              return(state);              /* Keep trying */

          default:
              error(badpack,prompt,type);
              return('A');       /* Some other packet type, "abort" */
    }
}                       /* End rinit()                          */




rpack(len,num,data)     /*  Read a Packet                       */
int *len, *num;                         /* Packet length, number */
char *data;                             /* Packet data */
{
    int i, done;                        /* Data character number, loop exit */
    char t,                             /* Current input character */
          cchksum,                        /* Our (computed) checksum */
          rchksum;                        /* Checksum received from other host */

    if (debug > 2)    printf(" ** rpack() ");
    timoset(timint);         /* set timeout                     */
/* The way timeouts are handled is that the flag stays clear, indicating
          that a timeout has occurred, until it is set to a value again
          by the next call to timoset().  This means that the effect can
          run up thro' the procedures without using longjmp().    */

    t = 0;
    *len = 0;                           /* in case times out    */
    while (t != SOH) {                  /* Wait for packet header */
          t = cread() & (char)0x7f;
          if (timflag == 0)
              return(FALSE);
    }

    done = FALSE;                       /* Got SOH, init loop */
    while (!done)                       /* Loop to get a packet */
    {
          t = cread();               /* Get character */
          if (timflag == 0)
              return(FALSE);
          if (t == SOH) continue;         /* Resynchronize if SOH */
          cchksum = t;                    /* Start the checksum */
          *len = unchar(t)-3;             /* Character count */

          t = cread();               /* Get character */
          if (timflag == 0)
              return(FALSE);
          if (t == SOH) continue;         /* Resynchronize if SOH */
          cchksum = cchksum + t;          /* Update checksum */
          *num = unchar(t);               /* Packet number */

          t = cread();               /* Get character */
          if (timflag == 0)
              return(FALSE);
          if (t == SOH) continue;         /* Resynchronize if SOH */
          cchksum = cchksum + t;          /* Update checksum */
          type = t;                       /* Packet type */

          for (i=0; i<*len; i++) {         /* The data itself, if any */
              t = cread();           /* Get character */
              if (timflag == 0)
                    return(FALSE);          /* as tho' bad checksum */
              if (t == SOH)
                    break;                  /* Resynch if SOH */
              cchksum = cchksum + t;      /* Update checksum */
              data[i] = t;                /* Put it in the data buffer */
          }
          if (t == SOH)
              continue;
          data[*len] = 0;                 /* Mark the end of the data */

          t = cread();               /* Get last character (checksum) */
          if (timflag == 0)
              return(FALSE);
          rchksum = unchar(t);            /* Convert to numeric */
          if (t == SOH)                   /* Resynchronize if SOH */
              continue;
          done = TRUE;                    /* Got checksum, done */
    }
    timocan();

    if (debug) fprintf(dfp,"Packet %d received; ",*num);
    if (debug>1) {                       /* Display incoming packet */
          fprintf(dfp," type: %c;",type);
          fprintf(dfp," num:  %d;",*num);
          fprintf(dfp," len:  %d.",*len);
          if (*len != 0) {
              fprintf(dfp,"\n  data: <");
              for (i=0; i<*len; ++i)
                    putc(data[i],dfp);
              putc('>',dfp);
    }   }
                                                  /* Fold in bits 7,8 to compute */
    cchksum = (((cchksum&0300) >> 6)+cchksum)&077; /* final checksum */
    if (cchksum != rchksum) {
          if (debug)
              fprintf(dfp,"\n bad checksum: rec'd %xx computed %xx; ",
                rchksum, cchksum);
          return(FALSE);
    }
    flushinput();

    return(type);                       /* All OK, return packet type */
}                               /* End of rpack()                       */




rpar(data,len)  /* instal received parameters safely            */
char    *data, len;
/* Set up incoming parameters to either what has been received
          or, if nil, to defaults.
  No return-code.                                               */
{
    char        p;

  /* initialize to defaults in case incoming block is short     */
    spsiz = 80;                 /* packet-length 80 chars       */
    timint = MYTIME;          /* timeout as defined           */
    eol = CR;                   /* terminator normally CR       */
    quote = '#';                /* standard control-quote char  */
    rptflg = FALSE;             /*  nor repeat-quoting          */

    while (len-- > 0) {
          p = data[len];

          switch (len) {          /* for each parameter           */

            case 0:                       /* MAXL                 */
              spsiz = unchar(p);
              break;

            case 1:                       /* TIME                 */
              timint = (unchar(p) < 5) ? 5 : unchar(p);
              break;

            case 2:                       /* NPAD                 */
              pad = unchar(p);
              break;

            case 3:                       /* PADC                 */
              padchar = ctl(p);
              break;

            case 4:                       /* EOL                  */
              eol = unchar(p);
              break;

            case 5:                       /* QCTL                 */
              quote = p;
              break;

            case 6:                       /* QBIN                 */
              if (image == 2) {
                    if (p == 'Y')
                        qu8 = '&';
                    else if (isalnum(p) == 0) /* provided punctuation */
                        qu8 = p;
                    else
                        image = 0;
                    break;
              }
              break;

            case 8:                       /* REPT                 */
              if (p == '~')
                    rptflg = TRUE;
              break;

            default:                      /* CHKT, CAPAS etc.     */
              break;

    }   }                       /* end while & outer switch     */

    if ( (qu8 == 0) && (image == 2) )   /* invlaid setting      */
          image = 0;
    if (debug)
          fprintf(dfp,"\nParameters in:  8th-bit setting now %d, char is %c; reptflag %s.",image,qu8,logicval[-rptflg]);
    return;
}                       /* End of rpar()                        */




char sbreak()           /* send EoT (break)                     */
{
    int num, len;                       /* Packet number, length */
    if (debug > 2)    printf(" ** sbreak() ");
    if (numtry++ > MAXTRY) return('A'); /* If too many tries "abort" */

    spack('B',n,0,packet);              /* Send a B packet */
    switch (rpack(&len,&num,recpkt))    /* What was the reply? */
    {
          case 'N':                       /* NAK, just stay in this state, */
              num = (--num<0 ? 63:num);   /* unless NAK for previous packet, */
              if (n != num)               /* which is just like an ACK for */
                    return(state);          /* this packet so fall thru to... */

          case 'Y':                       /* ACK */
              if (n != num) return(state); /* If wrong ACK, fail */
              numtry = 0;                 /* Reset try counter */
              n = (n+1)%64;               /* and bump packet count */
              return('C');                /* Switch state to Complete */

          case 'E':                       /* Error packet received */
              prerrpkt(recpkt);           /* Print it out and */
              return('A');                /* abort */

          case FALSE: return('C');        /* Receive failure, count as OK */
/* If timed out or etc. when completing, likely other end has gone away */

          default:
              error(badpack,prompt,type);
              return ('A');       /* Other, "abort" */
   }
}                       /* End sbreak()                         */




char sdata()            /* send data packet                      */
{
    int num, len;                       /* Packet number, length */

    if (debug > 2)    printf(" ** sdata() ");
    if (numtry++ > MAXTRY) return('A'); /* If too many tries, give up */

    spack('D',n,size,packet);           /* Send a D packet */
    switch(rpack(&len,&num,recpkt))     /* What was the reply? */
    {
          case 'N':                       /* NAK, just stay in this state, */
              num = (--num<0 ? 63:num);   /* unless it's NAK for next packet */
              if (n != num)               /* which is just like an ACK for */
                    return(state);          /* this packet so fall thru to... */

          case 'Y':                       /* ACK */
              if (n != num) return(state); /* If wrong ACK, persist */
              numtry = 0;                 /* Reset try counter    */
              n = (n+1)%64;               /* Bump packet count    */
              if (len != 0) switch (*recpkt) {   /* ACK has data */
                case 'Z':                         /* cancel all   */
                case 'z':
                    filecount = 0;                 /* no more to go */
                case 'X':                         /* cancel file  */
                case 'x':
                    return('Z');
                default:                          /* invalid      */
                    recpkt[20] = 0;                 /*  truncate    */
                    error("%sLocal Kermit sent ACK with data: <%s> - goodbye!",prompt,recpkt);
                    return('A');
              }
              if ((size = bufill(packet)) != EOF) /* data from file */
                    return('D');            /* Got data, stay in state D */
  /* EOF can mean either really end-of-file or an error         */
              if ( (num = filerr()) != 0 ) {    /* actual error */
                    error("Problem while reading file, OS code %xx",num);
                    return('A');
              }
              return('Z');                /* EOF                  */


          case 'E':                       /* Error packet received */
              prerrpkt(recpkt);           /* Print it out and */
              return('A');                /* abort */

          case FALSE: return(state);      /* Receive failure, stay in D */

          default:
              error(badpack,prompt,type);
              return('A');        /* Anything else, "abort" */
    }
}                       /* End sdata()                          */




sendsw()
/*  Sendsw is the state table switcher for sending files.  It loops until
 *  either it finishes, or an error is encountered.  The routines called
 *  by sendsw are responsible for changing the state.           */
{
    if (debug) {
          fprintf(dfp,"\nSendsw() sending file; ");
          if (debug > 2)
                    printf(" ** sendsw() ");
    }
    state = 'S';                        /* Send initiate is the start state */
    n = 0;                              /* Initialize message number */
    numtry = 0;                         /* Say no tries yet */
    forever {                           /* Do this as long as necessary */
          if (debug == 1) fprintf(dfp," sendsw state: %c\n",state);
          switch(state) {
              case 'S':   state = sinit();  break; /* Send-Init */
              case 'F':   state = sfile();  break; /* Send-File */
              case 'D':   state = sdata();  break; /* Send-Data */
              case 'Z':   state = seof();   break; /* Send-End-of-File */
              case 'B':   state = sbreak(); break; /* Send-Break */
              case 'C':   return (TRUE);           /* Complete */
              case 'A':   return (FALSE);          /* "Abort" */
              default:    return (FALSE);          /* Unknown, fail */
          }
    }
}                       /* End sendsw()                         */




char seof()             /*  Send EoF                               */
{
    int num, len;                       /* Packet number, length */
    if (debug > 2)    printf(" ** seof() ");
    if (numtry++ > MAXTRY) return('A'); /* If too many tries, "abort" */

    spack('Z',n,0,packet);              /* Send a 'Z' packet */
    switch(rpack(&len,&num,recpkt))     /* What was the reply? */
    {
          case 'N':                       /* NAK, just stay in this state, */
              num = (--num<0 ? 63:num);   /* unless it's NAK for next packet, */
              if (n != num)               /* which is just like an ACK for */
                    return(state);          /* this packet so fall thru to... */

          case 'Y':                       /* ACK */
              if (n != num) return(state); /* If wrong ACK, hold out */
              numtry = 0;                 /* Reset try counter */
              n = (n+1)%64;               /* and bump packet count */
              if (debug) fprintf(dfp,"\nClosing input file <%s>, ",filnam);
              fclose(fp);                 /* Close the input file */
              fp = NULL;                  /* Set flag indicating no file open */

              if (debug) fprintf(dfp,"looking for next file...");
              if (gnxtfl() == FALSE) {     /* No more files go? */
                    if (debug) fprintf(dfp,"\nNo more files to send.");
                    return('B');            /* if not, break, EOT, all done */
              }
              if (debug) fprintf(dfp,"New file is %s\n",filnam);
              return('F');                /* More files, switch state to F */

          case 'E':                       /* Error packet received */
              prerrpkt(recpkt);           /* Print it out and */
              return('A');                /* abort */

          case FALSE: return(state);      /* Receive failure, stay in Z */

          default:
              error(badpack,prompt,type);
              return('A');        /* Something else, "abort" */
    }
}                       /* End seof()                           */




char sfile()            /* send file header                     */
{
    int num, len;                       /* Packet number, length */
    char filnam1[50],                   /* Converted file name */
          *newfilnam,                     /* Pointer to file name to send */
          *cp;                            /* char pointer */

    if (debug > 2)    printf(" ** sfile() ");
    if (numtry++ > MAXTRY) return('A'); /* If too many tries, give up */

    if (fp == NULL) {                   /* If not already open, */
          if (debug) fprintf(dfp,"\nOpening %s for sending.",filnam);
          fp = fopen(filnam,"r");         /* open the file to be sent */
          if (fp == NULL)                 /* If bad file pointer, give up */
          {
              error("%sCannot open file <%s>",prompt,filnam);
              return('A');
          }
    }

    strcpy(filnam1, filnam);            /* Copy file name */
    newfilnam = cp = filnam1;
    while (*cp != '\0')                 /* Strip off all leading directory */
          if (*cp++ == '/')               /* names (ie. up to the last /). */
              newfilnam = cp;

    if (filnamcnv)                      /* Convert lower case to upper  */
          for (cp = newfilnam; *cp != '\0'; cp++)
              if (*cp >= 'a' && *cp <= 'z')
                    *cp ^= 040;

    len = cp - newfilnam;               /* Compute length of new filename */

    if (debug) fprintf(dfp,"\nSending %s as %s",filnam,newfilnam);

    spack('F',n,len,newfilnam);         /* Send an F packet */
    switch(rpack(&len,&num,recpkt))     /* What was the reply? */
    {
          case 'N':                       /* NAK, just stay in this state, */
              num = (--num<0 ? 63:num);   /* unless it's NAK for next packet */
          case 'Y':                       /* ACK */
              if (n != num)
                    return(state);          /* If wrong ACK, stay in F state */
              numtry = 0;                 /* Reset try counter */
              n = (n+1)%64;               /* Bump packet count */

              if ((size = bufill(packet)) != EOF) /* data from file */
                    return('D');            /* Got data, stay in state D */
  /* EOF can mean either really end-of-file or an error         */
              if ( (num = filerr()) != 0 ) {    /* actual error */
                    error("Problem while reading file, OS code %xx",num);
                    return('A');
              }
              return('Z');                /* EOF (empty file)     */

          case 'E':                       /* Error packet received */
              prerrpkt(recpkt);           /* Print it out and */
              return('A');                /* abort */

          case FALSE: return(state);      /* Receive failure, stay in F state */

          default:
              error(badpack,prompt,type);
              return('A');        /* Something else, just "abort" */
    }
}                       /* End sfile()                  */




char sinit()            /* send initiate (exchange parameters)  */
{
    int num, len;                       /* Packet number, length */

    if (debug > 2)    printf(" ** sinit() ");
    flushinput();                       /* Flush pending input */
    if (numtry++ > MAXTRY) return('A'); /* If too many tries, give up */
    len = spar(packet);                 /* Fill up init info packet */
    spack('S',n,len,packet);              /* Send an S packet */

    switch(rpack(&len,&num,recpkt))     /* What was the reply? */
    {
          case 'N':
              num = (--num<0 ? 63:num);   /* unless it's NAK for next packet */
              if (n != num)               /* which is just like an ACK for */
                    return(state);          /* this packet so fall thru to... */

          case 'Y':                       /* ACK */
              if (n != num)               /* If wrong ACK, stay in S state */
                    return(state);          /* and try again */
              rpar(recpkt,len);           /* Get other side's init info */
              numtry = 0;                 /* Reset try counter */
              n = (n+1)%64;               /* Bump packet count */
              return('F');                /* OK, switch state to F */

          case 'E':                       /* Error packet received */
              prerrpkt(recpkt);           /* Print it out and */
              return('A');                /* abort */

          case 'I':                       /* Init-parameters      */
              rpar(packet,len);
              len = spar(packet);
              spack('Y',n,len,packet); /* ack with our parameters */
              n = (n+1)%64;               /* Bump packet number, mod 64 */
              return(state);              /*  & continue          */

          case FALSE: return(state);      /* Receive failure, try again */

          default:
              error(badpack,prompt,type);
              return('A');           /* Anything else, just "abort" */
    }
}                       /* End sinit()                          */



spack(stype,num,len,data)        /*  Send a Packet               */
char stype, *data;
int num, len;
{
    int i;                              /* Character loop counter */
    char chksum, buffer[100];           /* Checksum, packet buffer */
    register char *bufp;                /* Buffer pointer */

    if (debug > 1) {                     /* Display outgoing packet */
          fprintf(dfp,"\nSending packet;  type: %c;",stype);
          fprintf(dfp," num: %d;",num);
          fprintf(dfp," len:  %d;",len);
          if (len != 0) {
              fprintf(dfp,"\n  data: <");
              for (i=0; i<len; ++i)
                    putc(data[i],dfp);
              putc('>',dfp);
    }   }

    bufp = buffer;                      /* Set up buffer pointer */
    for (i=1; i<=pad; i++) write(0,&padchar,1); /* Issue any padding */

    *bufp++ = SOH;                      /* Packet marker, ASCII 1 (SOH) */
    *bufp++ = tochar(len+3);            /* Send the character count */
    chksum  = tochar(len+3);            /* Initialize the checksum */
    *bufp++ = tochar(num);              /* Packet number */
    chksum += tochar(num);              /* Update checksum */
    *bufp++ = stype;                     /* Packet type */
    chksum += stype;                     /* Update checksum */

    for (i=0; i<len; i++)               /* Loop for all data characters */
    {
          *bufp++ = data[i];              /* Get a character */
          chksum += data[i];              /* Update checksum */
    }
    chksum = (((chksum&0300) >> 6)+chksum)&077; /* Compute final checksum */
    *bufp++ = tochar(chksum);           /* Put it in the packet */
    *bufp++ = eol;                      /* Extra-packet line terminator */
    *bufp = CR;                         /* CR for network       */
    write(0, buffer,bufp-buffer+1);     /* Send the packet */
    if (debug) {
          fprintf(dfp," Packet %d sent;\n",num);
          if (debug > 2)
              printf("\n\r");
    }
    return;
}                       /* End of spack()                       */




spar(data)              /* fill up packet with own parameters   */
char    *data;
/* returns length of parameter block (6, 7 or 9)                */
{
    char        len;

    data[0] = tochar(spsiz);
    data[1] = tochar(MYTIME);
    data[2] = tochar(MYPAD);
    data[3] = ctl(0);
    data[4] = tochar(MYEOL);
    data[5] = MYQUOTE;
    len = 6;
    if (image == 2) {                   /* 8th-bit prefixing    */
          if (qu8 == 0)
              data[6] = 'Y';
          else
              data[6] = qu8;              /*  feed back 8-quote   */
          len = 7;
    }
    else
          data[6] = 'N';
    if (rptflg) {               /* unless repeating turned off  */
          data[7] = '1';                  /*  1-byte checksum     */
          data[8] = '~';                  /*  only ~ for repeating*/
          len = 9;
    }
    if (debug)
          fprintf(dfp,"\nParameters out: 8th-bit setting now %d, char is %c; reptflag %s.",image,qu8,logicval[-rptflg]);
    return(len);
}                       /* End of spar()                        */




timocan()               /* cancel timeout               */
{
    timoset(0);
    timflag = 0xff;
    return;
}                       /* End of timocan()             */



timoex()                /* action timeout               */
{
    timflag = 0;                /* clear flag           */
    if (debug) {
          printmsg("Timeout ... ");
          fprintf(dfp,"Timeout ...");
    }
    return;
}                       /* End of timoex()              */



usage()         /*  Print summary of usage info and quit        */
{
    cooktty();
    printf("Usage: kermit  s[f78i] [-Ddebug-file] file(s) ... (send mode)\n");
    printf("or:    kermit  r[f78i] [-Ddebug-file]             (receive mode)\n");
    printf("or:    kermit  a[f78i] [-Ddebug-file]             (auto  mode)\n");
    printf("  where  f  =  filenames not converted to upper case,\n");
    printf("         7  =  force 7-bit data transfer,\n");
    printf("         8  =  attempt 8th-bit-prefixed transfer,\n");
    printf("         i  =  force 8-bit-image transfer,\n");
    printf("  and debug-file-name is preceded by \"-\" and up to 3 \"D\"-flags.\n");
    printf("or:    kermit  h                                  (help display).\n");
    printf(crlf);
    closeall();
    exit(0);
}                       /* End of usage()                       */



/**************  END of FILE  uclk??.c  **************************/
