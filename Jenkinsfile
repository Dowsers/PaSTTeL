pipeline {
  agent any
  stages {
    stage('Checkout') {
      steps {
        checkout([
          $class: 'GitSCM',
          branches: [[name: env.BRANCH_NAME]],
          userRemoteConfigs: scm.userRemoteConfigs,
          extensions: [
            [$class: 'CleanBeforeCheckout'],
            [$class: 'CloneOption', noTags: false, shallow: false],
            [$class: 'SubmoduleOption',
              disableSubmodules: false,
              parentCredentials: true,
              recursiveSubmodules: true,
              trackingSubmodules: false
            ]
          ],
          submoduleCfg: []
        ])
      }
    }

    stage('Build') {
      steps {
        sh 'docker compose build --no-cache'
      }
    }

    stage('Tests') {
      steps {
        sh 'mkdir -p test-reports'
        sh 'docker compose up -d'
        sh 'docker compose exec -T pasttel python3 scripts/test_non_regression.py'
      }
    }

    stage('Deploy') {
      when { branch 'main' }
      steps {
        echo 'Deploy...'
      }
    }
  }

  post {
    always {
      sh 'docker compose down'
      junit allowEmptyResults: true, testResults: 'test-reports/*.xml'
    }
    success {
      echo 'All tests passed.'
    }
    failure {
      echo 'Tests failed — check test-reports/.'
    }
  }
}
