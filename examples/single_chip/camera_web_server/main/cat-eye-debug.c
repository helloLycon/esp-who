#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/**
 *  调试改动的代码，免得调试时需要加入git版本
 */


const char *versionToUpgradeUrl(const char *version, char *upgradeUrl) {
    int verNum = atoi(strchr(version, '_') + 1) + 1;
    sprintf(upgradeUrl, "http://123.207.83.24/camera_web_server.bin");
    return upgradeUrl;
}

