//
// Created by shenhao on 2025/8/19.
//

#include "Util/CMD.h"
#include "Util/File.h"
#include "Util/mini.h"
#include "Utils/config_secure.h"
using namespace toolkit;
class CMD_main : public CMD {
public:
    CMD_main() {
        _parser.reset(new OptionParser(nullptr));
        (*_parser) << Option('c',                 /*该选项简称，如果是\x00则说明无简称*/
                             "config",            /*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgRequired, /*该选项后面必须跟值*/
                             nullptr,
                             true, /*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "配置文件路径", /*该选项说明文字*/
                             nullptr);

        (*_parser) << Option('d',                 /*该选项简称，如果是\x00则说明无简称*/
                             "dencrypt",               /*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgRequired, /*该选项后面必须跟值*/
                             nullptr,
                             false, /*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "解密配置文件", /*该选项说明文字*/
                             nullptr);
        (*_parser) << Option('e',                 /*该选项简称，如果是\x00则说明无简称*/
                             "encrypt",               /*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgRequired, /*该选项后面必须跟值*/
                             nullptr,
                             false, /*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "加密配置文件", /*该选项说明文字*/
                             nullptr);
    }

    ~CMD_main() override {}
    const char *description() const override { return "主程序命令参数"; }
};
#include <iostream>
#include <string>
using namespace std;

int main(int argc, char* argv[]){

    CMD_main cmd_main;
    try {
        cmd_main.operator()(argc, argv);
    } catch (ExitException &) { return 0; } catch (std::exception &ex) {
        cout << ex.what() << endl;
        return -1;
    }
    auto config_file = cmd_main["config"].as<std::string>();
    auto cfg = mINI::Instance();
    try {
        cfg.parseFile(config_file);
    }catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
    if(cmd_main.hasKey("encrypt")){
        if(store_conf(cfg)){
            std::cout << "encrypt success" << std::endl;
            cfg.dumpFile(cmd_main["encrypt"].as<std::string>());
        }else{
            std::cerr << "encrypt failed" << std::endl;
            return -1;
        }
    }
    if(cmd_main.hasKey("dencrypt")){
        if(load_conf(cfg)){
            cfg.dumpFile(cmd_main["dencrypt"].as<std::string>());
            std::cout << "dencrypt success" << std::endl;
        }else{
            std::cerr << "dencrypt failed" << std::endl;
            return -1;
        }
    }
    return 0;
}