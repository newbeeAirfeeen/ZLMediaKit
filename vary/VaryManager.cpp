//
// Created by 沈昊 on 2022/2/25.
//
#include "VaryManager.hpp"

VaryManager::Ptr VaryManager::Instance(){
    static VaryManager::Ptr manager(new VaryManager);
    return manager;
}

VaryTask::Ptr VaryManager::addTask(const std::string& id){
    std::lock_guard<std::mutex> lmtx(_mtx);
    auto it = tasks_map.find(id);
    if(it != tasks_map.end()){
        return it->second;
    }
    auto vary_task = std::make_shared<VaryTask>();
    tasks_map.emplace(id, vary_task);
    return vary_task;
}

void VaryManager::removeTask(const std::string& id){
    std::lock_guard<std::mutex> lmtx(_mtx);
    tasks_map.erase(id);
}

VaryTask::Ptr VaryManager::getTask(const std::string& id){
    std::lock_guard<std::mutex> lmtx(_mtx);
    auto it = tasks_map.find(id);
    if(it != tasks_map.end()){
        return it->second;
    }
    return nullptr;
}
