pipeline {
    agent {
        dockerfile {
            filename 'jenkins.Dockerfile'
            dir './'
            label 'linux-amd64'
        }
    }
    parameters {
        // 在 Jenkins UI 上选择目标架构：x86_64(原生) 或 arm64(交叉编译)
        choice(name: 'TARGET_ARCH', choices: ['x86_64', 'arm64'], description: '构建目标架构')
    }
    stages {
        stage('Before-Build') {
            steps {
                sh 'ls -al'
                sh 'gcc -v'
                // arm64 时额外打印交叉编译器版本，确认 >= GCC7
                sh '''
                    if [ "${TARGET_ARCH}" = "arm64" ]; then
                        ${CROSS_TRIPLE}-gcc -v
                    fi
                '''
            }
        }
        stage('Build'){
            steps{
                script {
                    def archive_name = "ZLMediakit-fmp4-${params.TARGET_ARCH}-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}.tar.gz"
                    // arm64 使用交叉编译工具链文件；strip 也需用对应架构的 strip
                    def cmake_extra = ""
                    def strip_cmd = "strip"
                    if (params.TARGET_ARCH == 'arm64') {
                        // 交叉 sysroot/三元组由 jenkins.Dockerfile 固定(CROSS_SYSROOT=/opt/aarch64-sysroot, CROSS_TRIPLE=aarch64-linux),
                        // 这些是容器环境变量,不在 Groovy 绑定里,故此处按 Dockerfile 约定写死路径
                        cmake_extra = "-DCMAKE_TOOLCHAIN_FILE=${WORKSPACE}/cmake/aarch64-linux-gnu.toolchain.cmake"
                        strip_cmd = "aarch64-linux-strip"
                    }
                    sh "cmake -B build \
                              -DCMAKE_BUILD_TYPE=Release \
                              -DCMAKE_INSTALL_PREFIX=${WORKSPACE}/out \
                              ${cmake_extra}"
                    sh '''
                           cmake --build build -- -j $(nproc)
                       '''
                    sh "cmake --install build"
                    sh "mkdir -p temp/lib"
                    sh "cp ${WORKSPACE}/out/bin/MediaServer temp/MediaServer"
                    sh "${strip_cmd} -s temp/MediaServer"
                    sh "cp ${WORKSPACE}/out/bin/configctl temp/configctl"
                    sh "${strip_cmd} -s temp/configctl"
                    sh "cp ${WORKSPACE}/out/bin/config.ini temp/config.ini"
                    sh "cp ${WORKSPACE}/out/lib/*.so* temp/lib"
                    sh "${strip_cmd} -s temp/lib/*.so*"
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
