/*
 * TEMPERATURE STUFF
 */
char *
gettemperature(char *base, char *sensor)
{
    char *co, *ret;

    co = readfile(base, sensor);
    if (co == NULL) {
        free(co);
        return smprintf("");
    }
    double temp = atof(co);
    free(co);
    ret = smprintf("%02.0f°C", temp / 1000);
    return ret;
}
