timestamps {
    node("ubuntu18-agent") {
        stage("Prerequisites"){
            sh '''
            echo $(pwd)
            if [ -d ./tests ]
            then
                if [ -d /home/ubuntu/fledge ]
                then 
                    cd /hom/ubuntu/fledge && old_sha=$(git rev-parse --short HEAD) && git pull  && new_sha=$(git rev-parse --short HEAD)
                    if [ ${old_sha} != ${new_sha} ]
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
        }
        checkout scm
        stage("Run tests"){
            echo "Running tests..."
            sh '''
                if [ -f requirements.sh ]
                then
                    ./requirements.sh
                fi
                export FLEDGE_ROOT=/home/ubuntu/fledge
                cd tests && cmake . && make && ./RunTests
            '''
            echo "Tests completed."
        }
    }
}