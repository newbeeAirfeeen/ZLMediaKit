# 第三方源码说明

ZLToolKit、jsoncpp 和 media-server 以普通目录纳入主仓库，不再使用 Git 子模块。
克隆本仓库、迁移到私有仓库或生成源码归档时，这三项依赖的源码会一并包含，
不需要访问原子仓库。构建所需的系统库和其他可选依赖仍按原有构建流程准备。

| 目录 | 上游来源 | 纳入时的基线提交 |
| --- | --- | --- |
| `ZLToolKit` | https://gitee.com/xia-chu/ZLToolKit | `c2137bb4c07cbefd62a7cf38f75c29d40b26e516` |
| `jsoncpp` | https://gitee.com/mirrors/jsoncpp.git | `8190e061bc2d95da37479a638aa2c9e483e58ec6` |
| `media-server` | https://gitee.com/ireader/media-server | `357ef885afa5112c8cb873eab21d94d26744c0ab` |

基线提交用于追溯来源；本仓库中的实际源码还包含定制修改。
此次迁移保留了三个目录的现有工作区内容，包括 ZLToolKit 中已有的
`src/Util/NoticeCenter.h` 修改，未执行上游更新。

后续定制修改直接提交到主仓库。需要合入上游更新时，先对比上述基线与目标版本，
再将经过审查的源码变更合入相应目录，并更新本说明中的基线记录。
各目录原有的许可证、版权声明和作者信息均保留。

使用 `docker/` 下的 Dockerfile 时，以仓库根目录作为构建上下文，例如：

```sh
docker build -f docker/ubuntu18.04/Dockerfile.runtime -t zlmediakit:local .
```

这些 Dockerfile 使用当前仓库中的源码，批量构建脚本 `docker/build_docker_images.sh`
也会自动选择仓库根目录作为构建上下文。
