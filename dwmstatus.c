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
#include <dirent.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

/************************
 * Configuration Options
 ************************/

/* Specify your timezone: */
#define TIMEZONE "America/Chicago"

/* Specify network device, usually wlan0 or eth0 */
#define NET_DEVICE "wlan0"

/* Specity path to your battery */
#define BATT_PATH "/sys/class/power_supply/BAT0"

/* Specify your temperature sensor information. */
#define TEMP_SENSOR_PATH "/sys/devices/platform/coretemp.0"
#define TEMP_SENSOR_UNIT "temp1_input"

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
    char *stat;
    char status;
	int descap, remcap;
    float remaining;
    float using;
    float voltage;
    float current;

	descap = -1;
	remcap = -1;
    using  = -1;
    remaining = -1;
    stat = "Not Present";

	co = readfile(base, "present");
	if (co == NULL || co[0] != '1') {
		if (co != NULL) free(co);
		return smprintf("not present");
	}
	free(co);

    co = readfile(base, "status");
    sscanf(co, "%s", &status);
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

    co = readfile(base, "power_now"); /* µWattage being used */
    if (co == NULL) {
        co = readfile(base, "voltage_now");
        sscanf(co, "%f", &voltage);
        free(co);
        co = readfile(base, "current_now");
        sscanf(co, "%f", &current);
        /* Remaining */
        remcap  = (voltage / 1000.0) * ((float)remcap / 1000.0);
        /* full design */
        descap  = (voltage / 1000.0) * ((float)descap / 1000.0);
        /* present rate */
        using = (voltage / 1000.0) * ((float)current / 1000.0);
        if (co == NULL)
            return smprintf("");
    }
    else
        sscanf(co, "%f", &using);
    free(co);


	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

    /* Getting time remaining */
    if (status == *("Discharging")) {
        remaining = (float)remcap / using;
        stat = "Batt";
    } else if (status == *("Charging")) {
        remaining = ((float)descap - (float)remcap) /using;
        stat = "Char";
    } else {
        remaining = 0;
        stat = "Full";
    }
    /* convert to hour:min:sec */
    int hours, seconds, minutes, secs_rem;
    secs_rem = (int)(remaining * 3600.0);
    hours = secs_rem / 3600;
    seconds = secs_rem - (hours * 3600);
    minutes = seconds / 60;
    seconds -= (minutes *60);

	return smprintf("%s: %.2f%s %02d:%02d:%02d", stat, (((float)remcap / (float)descap) * 100), "%", hours, minutes, seconds);
}
/* END BATTERY USAGE
 *
 * NETWORK STUFF
 * TODO ip address display (need new func, not in net/dev)
 * TODO connection status (Put in ip address func?)
 */
int 
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs, char *netdevice)
{
	char *buf;
    char *netstart;
	static int bufsize;
	FILE *devfd;

	buf = (char *) calloc(255, 1);
	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");

	/* ignore the first two lines of the file */
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
	    if ((netstart = strstr(buf, netdevice)) != NULL) {

		/* With thanks to the conky project at http://conky.sourceforge.net/ */
		sscanf(netstart + 6, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
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
get_netusage(char *netdevice)
{
	unsigned long long int oldrec, oldsent, newrec, newsent;
	double downspeed, upspeed;
	char *downspeedstr, *upspeedstr;
	char *retstr;
	int retval;

	downspeedstr = (char *) malloc(15);
	upspeedstr = (char *) malloc(15);
	retstr = (char *) malloc(42);

	retval = parse_netdev(&oldrec, &oldsent, netdevice);
	if (retval) {
	    fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
	    exit(1);
	}

	sleep(1);
	retval = parse_netdev(&newrec, &newsent, netdevice);
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
	sprintf(retstr, "%s: d: %s u: %s", netdevice, downspeedstr, upspeedstr);

	free(downspeedstr);
	free(upspeedstr);
	return retstr;
}

/* END NETWORK STUFF
 *
 * TEMPERATURE STUFF
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
    time_t now = time(NULL);

    if (difftime(now, *ltime) >= sec) {
        *ltime = now;
        return(1);
    }
    else
        return(0);
}


int
main(void)
{
	char *status = NULL;
	char *avgs = NULL;
    char *time = NULL;
    char *batt = NULL;
    char *net = NULL;
    char *temp = NULL;
    time_t count60 = 0;
    time_t count10 = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
        /* Update every minute */
        if (runevery(&count60, 60)) {
            free(time);

		    time  = mktimes("%Y/%d/%m %H:%M", TIMEZONE);
        }
        /* Update every 10 seconds */
        if (runevery(&count10, 10)) {
            free(avgs);
            free(batt);
            free(temp);

		    avgs   = loadavg();
            batt   = getbattery(BATT_PATH);
            temp   = gettemperature(TEMP_SENSOR_PATH, TEMP_SENSOR_UNIT);
        }
        /* Update every second */
        net    = get_netusage(NET_DEVICE);

        /* Format of display */
		status = smprintf("%s | %s | [%s] | T: %s | %s",
				net, batt, avgs, temp, time);
        free(net);
		setstatus(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

