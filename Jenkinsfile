timestamps {
    node("ubuntu18-agent") {
        catchError {
            checkout scm
            dir_exist= sh (
		        script: "test -d 'tests' && echo 'Y' || echo 'N' ",
                returnStdout: true
            ).trim()

            if (dir_exist == 'N'){
                currentBuild.result= 'FAILURE'
                echo "No tests directory found! Exiting."
                return
            }
            // Set BRANCH to specific branch of fledge repo, in case you want to run the tests against specific code
            // e.g. FOGL-xxxx, main etc.
            try {
                stage("Prerequisites"){
                    sh '''
                        BRANCH='develop'
                        ${HOME}/buildFledge.sh ${BRANCH} ${WORKSPACE}
                    '''
                }
            } catch (e) {
                currentBuild.result = 'SUCCESS'
                echo "Failed to build Fledge; required to run the tests."
                return
            }   

            try { 
                stage("Run tests"){
                    echo "Executing tests..."
                    sh '''
                        export FLEDGE_ROOT=$HOME/fledge
                        cd tests && cmake . && make && ./RunTests --gtest_output=xml:test_output.xml
                    '''
                    echo "Done."
                }
            } catch (e) {
                result = "TEST FAILED" 
                currentBuild.result = 'FAILURE'
                echo "Tests failed."
            }

            try {
                stage("Publish Report"){
                    echo "Archiving XML Repport"
                    junit "tests/test_output.xml"
                }
            } catch (e) {
                result = "TEST REPORT GENERATION FAILED"
                currentBuild.result = 'FAILURE'
                echo "Failed to generate test reports!"
            }
        }

        stage ("Cleanup"){
            // Add here if any cleanup is required
            echo "Done."
        }
    }
}