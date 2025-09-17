//
// Created by shenhao on 2025/8/18.
//

#ifndef NETWORKTOOL_CONFIG_SECURE_H
#define NETWORKTOOL_CONFIG_SECURE_H

#if defined(ENABLE_OPENSSL)
#include "Common/config.h"
bool store_conf(toolkit::mINI_basic<std::string, toolkit::variant>& ini);
bool load_conf(toolkit::mINI_basic<std::string, toolkit::variant>& ini);
#endif




#endif //NETWORKTOOL_CONFIG_SECURE_H
