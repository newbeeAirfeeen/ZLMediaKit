//
// Created by 沈昊 on 2022/2/24.
//

#ifndef MEDIAKIT_VARYSTATUS_HPP
#define MEDIAKIT_VARYSTATUS_HPP
namespace TaskStatus{

    /* 任务正在初始化 */
    static constexpr const int Initing             =  1;
    /* 任务未知状态 */
    static constexpr const int unknown             =  2;
    /* 任务即将被关闭 */
    static constexpr const int die                 =  3;
    /* 拉流成功 */
    static constexpr const int pull_success        =  4;
    /* 拉流方面的错误 */
    static constexpr const int pull_timeout        = -1;
    /* dns 解析失败 */
    static constexpr const int pull_dns_err        = -2;
    /* 对端主动关闭连接 */
    static constexpr const int pull_ative_err      = -3;
    /* 连接被重置 */
    static constexpr const int pull_reset_conn     = -4;
    /* 拉流端的通道为空 */
    static constexpr const int pull_track_empty    = -5;
    /**************************************************/
    /* 编解码器 */
    static constexpr const int coder_initing       =  6;
    static constexpr const int coder_init_sucess   =  7;
    static constexpr const int coder_init_failed   = -8;
    /**************************************************/
    /* 推流方面 */
    /* 推流成功 */
    static constexpr const int push_success        =  9;
    /* 尝试正在推流 */
    static constexpr const int pushing             =  10;
    /* 推流超时 */
    static constexpr const int push_timeout        = -9;
    /* 推流dns解析错误 */
    static constexpr const int push_dns_err        = -10;
    /* 收流端主动关闭 */
    static constexpr const int push_ative_err      = -11;
    /* 收流端重置连接 */
    static constexpr const int push_reset_conn     = -12;
    /* 其他错误 */
    static constexpr const int other_err           = -999;

}

#endif // MEDIAKIT_VARYSTATUS_HPP
