#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

/*char *tzutc = "UTC";*/
char *tzchicago = "America/Chicago";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	if (fd == NULL)
		return NULL;
	free(path);

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}
/* BATTERY USAGE
 * Linux seems to change the filenames after suspend/hibernate
 * according to a random scheme. So just check for both possibilities.
 */
char *
getbattery(char *base)
{
	char *co;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL || co[0] != '1') {
		if (co != NULL) free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.2f", ((float)remcap / (float)descap) * 100);
}
/* END BATTERY USAGE
 */

/* NETWORK STUFF
 */
int 
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
	char *buf;
	char *wlan0start;
	static int bufsize;
	FILE *devfd;

	buf = (char *) calloc(255, 1);
	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");

	// ignore the first two lines of the file
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
	    if ((wlan0start = strstr(buf, "wlan0:")) != NULL) {

		// With thanks to the conky project at http://conky.sourceforge.net/
		sscanf(wlan0start + 6, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
		       receivedabs, sentabs);
		fclose(devfd);
		free(buf);
		return 0;
	    }
	}
	fclose(devfd);
	free(buf);
	return 1;
}

char *
get_netusage()
{
	unsigned long long int oldrec, oldsent, newrec, newsent;
	double downspeed, upspeed;
	char *downspeedstr, *upspeedstr;
	char *retstr;
	int retval;

	downspeedstr = (char *) malloc(15);
	upspeedstr = (char *) malloc(15);
	retstr = (char *) malloc(42);

	retval = parse_netdev(&oldrec, &oldsent);
	if (retval) {
	    fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
	    exit(1);
	}

	sleep(1);
	retval = parse_netdev(&newrec, &newsent);
	if (retval) {
	    fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
	    exit(1);
	}

	downspeed = (newrec - oldrec) / 1024.0;
	if (downspeed > 1024.0) {
	    downspeed /= 1024.0;
	    sprintf(downspeedstr, "%.3f MB/s", downspeed);
	} else {
	    sprintf(downspeedstr, "%.2f KB/s", downspeed);
	}

	upspeed = (newsent - oldsent) / 1024.0;
	if (upspeed > 1024.0) {
	    upspeed /= 1024.0;
	    sprintf(upspeedstr, "%.3f MB/s", upspeed);
	} else {
	    sprintf(upspeedstr, "%.2f KB/s", upspeed);
	}
	sprintf(retstr, "d: %s u: %s", downspeedstr, upspeedstr);

	free(downspeedstr);
	free(upspeedstr);
	return retstr;
}

/* END NETWORK STUFF
 */
/* Temp stuff
 * gettemperature("/sys/class/hwmon/hwmon0/device", "temp1_input");
 */
char *
gettemperature(char *base, char *sensor)
{
    char *co;

    co = readfile(base, sensor);
    if (co == NULL)
        return smprintf("");
    return smprintf("%02.0f°C", atof(co) / 1000);
}
/* END TEMP STUFF
 */

int runevery(time_t *ltime, int sec) 
{
    /* return 1 if 'sec' elapsed since last run
     * else return 0
     */
    time_t now = time(NULL);

    if (difftime(now, *ltime) >= sec) {
        *ltime = now;
        return(1);
    }
    else
        return(0);
}

char *
get_freespace(char *mntpt)
{
    struct stavfs data;
    double total, used = 0;
    if ((statvfs(mntpt, &data)) < 0) {
        fprintf(stderr, "can't get info on disk.\n");
        return("?");
    }
    total = (data.f_blocks * data.f_frsize);
    used  = (data.f_blocks - data.f_bfree) * data.f_frsize;
    return smrpintf("%.1f", (used/total) * 100);
}


int
main(void)
{
	char *status;
	char *avgs;
    char *tmchi;
    char *batt;
    char *net;
    char *temp;
    char *rootfs;
    char *homefs;
    time_t count5min = 0;
    time_t count1min = 0;
    time_t count10sec = 0;
/*    time_t count5sec = 0;*/

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
        /* checks every 5 minutes */
        if (runevery(&count5min, 300)) {
            free(homefs);
            free(rootfs);

            homefs = get_freespace("/home");
            rootfs = get_freespace("/");
        }
        /* checks every minute */
        if (runevery(&count1min, 60)) {
            free(tmchi);
            
            avgs   = loadavg();
		    tmchi  = mktimes("%H:%M", tzchicago);
        }
        /* checks every 10 seconds */
        if (runevery(&count10sec, 10)) {
            free(avgs);
            free(temp);
		    
            avgs   = loadavg();
            temp   = gettemperature("/sys/devices/platform/coretemp.0/", "temp1_input");
        }
        /* checks every 5 seconds */
        /* NONE */
        free(net)
		free(status);
            
        net    = get_netusage();

		status = smprintf(" / %s% | /home %s% | wlan0: %s | Batt: %s% | Load: [%s] | Temp: %s | %s",
				rootfs, homefs, net, batt, avgs, temp, tmchi);
		setstatus(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

