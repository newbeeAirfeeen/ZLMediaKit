pipeline {
    agent {
        dockerfile {
            filename 'jenkins.Dockerfile'
            dir './'
            // 由 TARGET_ARCH 决定构建镜像的平台：arm64 走 qemu 模拟的 CentOS7-arm64 原生编译
            additionalBuildArgs "--platform=linux/${params.TARGET_ARCH == 'arm64' ? 'arm64' : 'amd64'}"
            label 'linux-amd64'
        }
    }
    parameters {
        // Jenkins UI 选择目标架构：x86_64(原生) 或 arm64(qemu 模拟下原生编译)
        choice(name: 'TARGET_ARCH', choices: ['x86_64', 'arm64'], description: '构建目标架构')
    }
    stages {
        stage('Before-Build') {
            steps {
                sh 'uname -m'
                sh 'gcc -v'
            }
        }
        stage('Build'){
            steps{
                script {
                    def archive_name = "ZLMediakit-fmp4-${params.TARGET_ARCH}-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}.tar.gz"
                    // 清理持久化工作区可能残留的旧 cmake 缓存(切换构建方式后必须),避免复用过期 toolchain/编译器
                    sh "rm -rf build out temp target"
                    sh "cmake -B build \
                              -DCMAKE_BUILD_TYPE=Release \
                              -DCMAKE_INSTALL_PREFIX=${WORKSPACE}/out"
                    sh '''
                           cmake --build build -- -j $(nproc)
                       '''
                    sh "cmake --install build"
                    sh "mkdir -p temp/lib"
                    sh "cp ${WORKSPACE}/out/bin/MediaServer temp/MediaServer"
                    sh "strip -s temp/MediaServer"
                    sh "cp ${WORKSPACE}/out/bin/configctl temp/configctl"
                    sh "strip -s temp/configctl"
                    sh "cp ${WORKSPACE}/out/bin/config.ini temp/config.ini"
                    sh "cp ${WORKSPACE}/out/lib/*.so* temp/lib"
                    sh "strip -s temp/lib/*.so*"
                    sh "mkdir -p target"
                    sh "tar -zcvf ${archive_name} -C temp ."
                    sh "mv ${archive_name} target/"
                    sh "rm -rf temp"
                }
            }
        }
        stage('Archive') {
            steps {
                script{
                    def archive_name = "ZLMediakit-fmp4-${params.TARGET_ARCH}-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}.tar.gz"
                    archiveArtifacts artifacts: "target/${archive_name}", followSymlinks: false, onlyIfSuccessful: true
                }
            }
        }
    }
    post {
        success {
            echo 'build success'
        }
        unsuccessful {
            echo 'build failed'
        }
    }
}
