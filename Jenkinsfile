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
    environment {
        SMB_SHARE = '//172.29.10.109/X-Lab'
        SMB_DIRECTORY = 'zlm-wlw'
        SMB_DOMAIN = "${params.SMB_DOMAIN}"
        RELEASE_PROJECT = 'ZLMediakit-fmp4'
    }
    parameters {
        string(name: 'SMB_CREDENTIALS_ID', defaultValue: 'smb-172-29-10-109', description: 'Jenkins 用户名/密码凭据 ID，需要对应共享的读写和重命名权限')
        string(name: 'SMB_DOMAIN', defaultValue: 'CLOSELI', description: 'SMB 认证域；该服务器使用 CLOSELI，用户名和密码保存在 Jenkins 凭据中')
        // Jenkins UI 选择目标架构：x86_64(原生) 或 arm64(qemu 模拟下原生编译)
        choice(name: 'TARGET_ARCH', choices: ['x86_64', 'arm64'], description: '构建目标架构')
    }
    stages {
        stage('Before-Build') {
            steps {
                script {
                    env.SMB_STATUS = '未归档'
                    // 保持原有包名；标题便于列表定位，完整版本和下载文件见构建详情。
                    env.RELEASE_VERSION = "ZLMediakit-fmp4-${params.TARGET_ARCH}-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}"
                    env.ARCHIVE_NAME = "${env.RELEASE_VERSION}.tar.gz"
                    // 限制文件名为单一路径组件，避免构建参数带入目录或 shell 语法。
                    if (!(env.ARCHIVE_NAME ==~ /[A-Za-z0-9_.-]+/)) {
                        error('包名包含非法字符，请检查 BUILD_NAME / NODE_NAME')
                    }
                    currentBuild.displayName = "#${env.BUILD_NUMBER} ${env.RELEASE_PROJECT} ${params.TARGET_ARCH} ${env.GIT_COMMIT.take(8)}"
                    currentBuild.description = "版本: ${env.RELEASE_VERSION}\nSMB: 待归档"
                }
                withCredentials([usernamePassword(credentialsId: params.SMB_CREDENTIALS_ID,
                        usernameVariable: 'SMB_USERNAME', passwordVariable: 'SMB_PASSWORD')]) {
                    sh(label: '检查 SMB 连接', script: 'bash scripts/jenkins-publish-smb.sh check')
                }
                sh 'uname -m'
                sh 'gcc -v'
            }
        }
        stage('Build'){
            steps{
                script {
                    def archive_name = env.ARCHIVE_NAME
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
                script {
                    sh(label: '生成包校验值', script: '''
                        set -eu
                        cd target
                        sha256sum "$ARCHIVE_NAME" > "$ARCHIVE_NAME.sha256"
                    ''')
                    def checksum = readFile("target/${env.ARCHIVE_NAME}.sha256").trim().tokenize()[0]
                    def builtAt = sh(script: 'date -u +%Y-%m-%dT%H:%M:%SZ', returnStdout: true).trim()
                    def info = """project=${env.RELEASE_PROJECT}
version=${env.RELEASE_VERSION}
architecture=${params.TARGET_ARCH}
commit=${env.GIT_COMMIT}
branch=${env.BRANCH_NAME ?: env.GIT_BRANCH ?: ''}
buildNumber=${env.BUILD_NUMBER}
buildTimeUtc=${builtAt}
buildUrl=${env.BUILD_URL}
package=${env.ARCHIVE_NAME}
sha256=${checksum}
smbDirectory=smb:${env.SMB_SHARE}/${env.SMB_DIRECTORY}/${env.RELEASE_VERSION}
"""
                    writeFile file: "target/${env.ARCHIVE_NAME}.version.txt", text: info, encoding: 'UTF-8'
                    echo info
                    // 先归档 Jenkins；SMB 故障时仍可在本次构建页面下载产物。
                    archiveArtifacts artifacts: 'target/*.tar.gz,target/*.sha256,target/*.version.txt',
                        followSymlinks: false, onlyIfSuccessful: true, fingerprint: true
                }
            }
        }
        stage('Publish SMB') {
            steps {
                script {
                    try {
                        // 凭据只在上传阶段绑定，Groovy 不插值密码，容器无需挂载 SMB。
                        withCredentials([usernamePassword(credentialsId: params.SMB_CREDENTIALS_ID,
                                usernameVariable: 'SMB_USERNAME', passwordVariable: 'SMB_PASSWORD')]) {
                            sh(label: 'SMB 归档并校验', script: 'bash scripts/jenkins-publish-smb.sh publish')
                        }
                        env.SMB_STATUS = '已归档'
                    } catch (err) {
                        env.SMB_STATUS = '归档失败'
                        throw err
                    }
                }
            }
        }
    }
    post {
        always {
            script {
                if (env.RELEASE_VERSION) {
                    currentBuild.description = "版本: ${env.RELEASE_VERSION}\nSMB: ${env.SMB_STATUS}\n路径: smb:${env.SMB_SHARE}/${env.SMB_DIRECTORY}/${env.RELEASE_VERSION}/${env.ARCHIVE_NAME}"
                }
            }
        }
        success {
            echo 'build success'
        }
        unsuccessful {
            echo 'build failed'
        }
    }
}
