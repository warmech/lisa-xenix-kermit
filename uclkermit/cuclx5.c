/* File uclk5md.c	Icarus Sparry, Bath University
	Machine-dependent parts of UCL Kermit.

        Modified to run on UNIX System 5 as on GEC 63 systems */

/* Revision History:
	0:	First attempt. 24 Oct 85
		Changed sttyb to termio, deleted all code in #ifdef BSD
		changed stty,gtty to ioctl and altered flags
*/

/*	The following routines are possibly machine or system dependent:
	it is believed that all such dependencies in the code
	have been grouped here. */

#include	<stdio.h>
#include	<termio.h>
#include	<signal.h>


/* Variables in main file: */
extern	char	timflag;
extern	int	debug, image, timoex();
extern	FILE	*dfp, *fp;

#define CR	13
#define LF	10

struct	termio	cookedmode, rawmode;

char ascedit(c)		/* edit character for sending */
/*	This filtering routine converts unix LF line-terminator
	to CR/LF pair iff in 7-bit mode.
	Returns modified char. */
char	c;
{
	if (image == 0) {	/* only if 7-bit */
		c &= 0x7f;
		if (c == LF)
			encode(CR);	/* CR of CR/LF; */
	}	/* bufill() adds the LF */
	return(c);
}	/* End of ascedit() */

char ascout(a)		/* char to file coping with CR/LF */
/* This filtering routine is used to write 7-bit data to file as
	it is received. Not used for 8-bit data, either image
	or 8-prefixing. */
char	a;
{
	char	ret;
	static	char	olda = 0;

	/* for unix, replace each CR, LF or CR/LF or LF/CR by a single LF */
	if ( ( (a == CR) && (olda == LF) ) || ( (a == LF) && (olda == CR) ) )
		ret = olda = 0;
	else
	{	/* if not CR/LF pair */
		olda = a;
		if (a == CR) a = LF;	/* CR => LF for unix */
	/* if converting for non-unix system, probably need LF => CR instead */
		ret = putc(a,fp);
	}
	return (ret);
}	/* End of ascout() */

char	filerr()		/* return error-code or 0 */
/* called when EOF encountered reading or writing to data-file;
	if really endfile, returns 0, else system error-code. */
{
	char	ret;

	ret = ferror(fp);
	clearerr(fp);
	return(ret);
}	/* End of filerr() */

flushinput()		/* UCL 2.8 version */
/* Dump all pending input to clear stacked up NAK's.
 * Note that because of a bug in unix 2.8, this also flushes OUTPUT! */
{
	ioctl(0,TCFLSH,0);
}	/* End of flushinput() */

/* Routines to set terminal input into "raw" or "cooked" mode. */

static	char	cookedok = 0;

cooktty()		/* restore terminal state */
{
	if (cookedok != 0)		/* provided made raw */
		ioctl(0,TCSETAW,&cookedmode);
}	/* End of cooktty */

rawtty()		/* set terminal raw */
{
	if (cookedok == 0) {	/* first time only */
		ioctl(0,TCGETA,&cookedmode); /* Save current mode so we can */
		ioctl(0,TCGETA,&rawmode);	/* restore it later */
		cookedok = 1;
		rawmode.c_iflag = IXON|IXOFF;
		rawmode.c_oflag &= ~OPOST;
		rawmode.c_lflag = 0;
		rawmode.c_cc[VEOF] = 1;		/* Require at least one */
	}
	ioctl(0,TCSETAW,&rawmode);	/* Put tty in raw mode */
}	/* End of rawtty() */

/* end of tty cook/uncook routines */

/* UCL Input & Timeout Routines */

/* Timeouts are always accompanied by attempting to read
	the line. Two situations are catered for: normally,
	if a timeout can be set which cancels a hanging
	read() call, then this is done; else if a test
	can be made as to whether chars are available AND
	a sleep() call is available, these are linked to
	provide a timeout based on decrementing after
	each sleep() [this is the "Berkeley 4.2" situation].
	The following code uses "#ifdef BSD42" to differentiate
	between "4.2" & "normal"; this has been hacked into
	the VAX C-compiler at UCL but can be inserted into
	the text elsewhere. */

char nextin()		/* read next char, checking time-flag */
/* return char (or 0 if timer expired) */
{
	char	c;
	static char buff[64];
	static int count = 0;
	long	lcount;

	/* this code if timeout cancels read() */
	read (0,&c,1);
	if (debug > 2)
		fprintf(dfp," next = %xx ",((int)c)&0xff );
	return(c);
}	/* end of nextin() */



timoset(sex)		/* set timeout */
char	sex;	/* # of seconds */
{
	timflag = sex;
	/* only set timeout-alarms if non-4.2 code being used */
	signal(SIGALRM,timoex);
	alarm(timflag);
}	/* End of timoset() */

/* The routines which action and clear timeouts, timoex()
	and timocan() are not system-dependent; see above. */

unbuffer()		/* unbuffer output */
/* system-dependent action to write quickly to terminal */
{
	setbuf(stdout,0);	/* UNbuffer output! */
}	/* End of unbuffer() */

/* End of File */
