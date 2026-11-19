# The m*.cpp files include ../../<project>/main/model.cpp of the 11 model
# projects, so this project must stay inside the esp32/ folder next to them.
idf_component_register(SRCS "main.cpp"
                            "m01_decision_tree.cpp" "m02_random_forest.cpp" "m03_xgboost.cpp"
                            "m04_logistic_regression.cpp" "m05_svm.cpp" "m06_svm_rbf.cpp"
                            "m07_naive_bayes.cpp" "m08_kmeans.cpp" "m09_mlp.cpp"
                            "m10_cnn1d.cpp" "m11_autoencoder.cpp"
                       INCLUDE_DIRS ".")
