/* See LICENSE file for copyright and license details */

/************************
 * Configuration Options
 ************************/

/* Specify your timezone: */
#define TIMEZONE "America/New_York"

/* Specify network device, usually wlan0 or eth0
 * systemd has some new naming conventions you can turn on with longer names.
 */
#define NET_DEVICE_PRIMARY "wlan0"
/* Currently not of any use  */
#define NET_DEVICE_SECONDARY "eth0"

/* Specity path to your battery */
#define BATT_PATH "/sys/class/power_supply/BAT0"

/* Specify your temperature sensor information. */
#define TEMP_SENSOR_PATH "/sys/devices/platform/coretemp.0"
#define TEMP_SENSOR_UNIT "temp1_input"
