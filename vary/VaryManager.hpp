//
// Created by 沈昊 on 2022/2/25.
//

#ifndef MEDIAKIT_VARYMANAGER_HPP
#define MEDIAKIT_VARYMANAGER_HPP

#include <mutex>
#include <memory>
#include <unordered_map>
#include "VaryTask.hpp"
class VaryManager: public std::enable_shared_from_this<VaryManager>{
public:
    using Ptr = std::shared_ptr<VaryManager>;
public:
    static VaryManager::Ptr Instance();
    ~VaryManager() = default;
public:
    /*
     * 添加一个任务
     * */
    VaryTask::Ptr addTask(const std::string& id);
    /*
     * 获取一个任务
     * */
    VaryTask::Ptr getTask(const std::string& id);
    /*
     * 删除一个任务
     * */
    void removeTask(const std::string& id);
private:
    VaryManager() = default;

private:
    std::mutex _mtx;
    std::unordered_map<std::string, VaryTask::Ptr> tasks_map;
};







#endif // MEDIAKIT_VARYMANAGER_HPP
