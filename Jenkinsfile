timestamps {
    node("ubuntu18-agent") {
        catchError{
            stage("Prerequisites"){
                try {
                sh '''
                echo $(pwd)
                if [ -d ./tests ]
                then
                    if [ -d /home/ubuntu/fledge ]
                    then 
                        cd /hom/ubuntu/fledge && old_sha=$(git rev-parse --short HEAD) && git pull  && new_sha=$(git rev-parse --short HEAD)
                        if [[ ${old_sha} != ${new_sha} ]];
                        then
                            sudo ./requirements.sh && make
                        fi
                    else
                        cd /home/ubuntu && git clone https://github.com/fledge-iot/fledge && cd fledge && sudo ./requirements.sh && make
                    fi
                    cd /home/ubuntu/fledge; git log -n 1;
                else
                    echo "tests directory does not exist"
                    exit 1
                fi
                '''
                } catch (e) {
                    currentBuild.result = 'FAILURE'
                    return
                }
            }
            checkout scm
            stage("Run tests"){
                echo "Running tests..."
                try {
                sh '''
                    if [ -f requirements.sh ]
                    then
                        ./requirements.sh
                    fi
                    export FLEDGE_ROOT=/home/ubuntu/fledge
                    cd tests && make && ./RunTests
                '''
                } catch (e) {
                    currentBuild.result = 'FAILURE'
                    return
                }
                echo "Tests completed."
            }
        }
    }
}