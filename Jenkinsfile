pipeline {
    agent {
        dockerfile {
            filename 'jenkins.Dockerfile'
            dir './'
            label 'linux-amd64'
        }
    }
    stages {
        stage('Before-Build') {
            steps {
                sh 'ls -al'
                sh 'gcc -v'
            }
        }
        stage('Build'){
            steps{
                script {
                    def archive_name = "ZLMediakit-fmp4-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}.tar.gz"
                    sh "cmake -B build -DCMAKE_BUILD_TYPE=Release \
                                       -DCMAKE_INSTALL_PREFIX=${WORKSPACE}/out"
                        "
                    sh '''
                           cmake --build build -- -j $(nproc)
                           cmake --install build
                       '''
                    sh "mkdir -p temp"

                    sh "cp ${WORKSPACE}/out/bin/MediaServer temp/MediaServer"
                    sh "cp ${WORKSPACE}/out/bin/config.ini temp/config.ini"
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
                    def archive_name = "ZLMediakit-fmp4-${env.NODE_NAME}.${env.BUILD_ID}.${env.GIT_COMMIT}.tar.gz"
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