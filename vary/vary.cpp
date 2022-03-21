#include <Poller/EventPoller.h>
#include <Util/onceToken.h>
#include <Network/TcpServer.h>
#include <Http/HttpSession.h>
#include <Common/config.h>
#include "VaryTask.hpp"
#include "jsoncpp/json.h"
#include "VaryManager.hpp"
using namespace toolkit;
using namespace mediakit;
void initEventListener(){
    /**
     *
     * {
     *      "session_id": "123132",
     *      "cmd": "vary | stop"
     *      "src_url"
     *      "dst_url"
     *      "video":{
     *          "codec" :
     *          "width" :
     *          "height":
     *      }
     *
     * }
     * */
    static onceToken s_token([](){
        NoticeCenter::Instance().addListener(nullptr,Broadcast::kBroadcastHttpRequest,[](BroadcastHttpRequestArgs){
            //const Parser &parser,HttpSession::HttpResponseInvoker &invoker,bool &consumed
            if(strstr(parser.Url().data(),"/api/vary") != parser.Url().data()){
                return;
            }
            if(parser.Method() != "POST"){
                return;
            }

            //url以"/api/起始，说明是http api"
            consumed = true;//该http请求已被消费
            const std::string& content = parser.Content();
            InfoL << content;
            Json::CharReaderBuilder reader_builder;
            Json::CharReader* reader = reader_builder.newCharReader();
            Json::Value val;
            std::string err;
            if(!reader->parse(content.data(), content.data() + content.size(), &val, &err)){
                HttpSession::KeyValue headerOut;
                invoker(401, headerOut, "请求参数有误");
                return;
            }
            const std::string& session_id = val.get("session_id","").asString();
            const std::string& cmd = val.get("cmd", "").asString();
            const std::string& src_url = val.get("src_url", "").asString();
            const std::string& dst_url = val.get("dst_url","").asString();
            const Json::Value vcodec = val.get("video", {});


            if( cmd == "stop" ){
                VaryManager::Instance()->removeTask(session_id);

                return;
            }



//            vary_session = std::make_shared<VarySession>();
//            vary_session->setExceptionHandler([invoker](const std::string& str){
//                EventPollerPool::Instance().getPoller(false)->async([invoker, str](){
//                    HttpSession::KeyValue headerOut;
//                    invoker(500,headerOut, str);
//                });
//            });
//            vary_session->setOnSuccess([invoker](){
//                EventPollerPool::Instance().getPoller(false)->async([invoker](){
//                    HttpSession::KeyValue headerOut;
//                    //你可以自定义header,如果跟默认header重名，则会覆盖之
//                    //默认header有:Server,Connection,Date,Content-Type,Content-Length
//                    //请勿覆盖Connection、Content-Length键
//                    //键名覆盖时不区分大小写
//                    //headerOut["TestHeader"] = "HeaderValue";
//                    invoker(200,headerOut,"success");
//                });
//            });
//            vary_session->Excute(val);
        });

    }, nullptr);
}

int main(){

    //设置退出信号处理函数
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// 设置退出信号

    //设置日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    EventPollerPool pool = EventPollerPool::Instance();
    //初始化管理器
    //initEventListener();
    //VarySession::Ptr vary_session = std::make_shared<VarySession>();
    //开启http服务器
    //TcpServer::Ptr httpSrv(new TcpServer());
    //httpSrv->start<HttpSession>(8080);//默认80

    VaryTask::Ptr  task = std::make_shared<VaryTask>(EventPollerPool::Instance().getPoller(false));
    task->start_l();


    sem.wait();
    return 0;
}