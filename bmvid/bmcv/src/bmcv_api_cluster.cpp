#include "bmcv_api_ext.h"
#include "bmcv_bm1684x.h"
#include "bmcv_internal.h"
#include "bmlib_runtime.h"
#include "string.h"
#include <fstream>

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <iostream>
#include <random>

#ifdef __linux__
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#if defined(__linux__) && defined(USING_OPENBLAS)

#define USING_REF  0

#define CONST_WEIGHT  0
#define POISSON_CPP    1
#define MT19937_CPP   2

#define UNIVERSAL_SEED 42

#define TIME_COST_S(start, end) (((end.tv_sec - start.tv_sec) * 1000000.0 + (end.tv_usec - start.tv_usec)) / 1000000.0)

bm_status_t bmcv_cluster_check(bm_handle_t handle, int row, int col, int p, int weight_mode_KNN) {
    if (handle == NULL) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "Can not get handle!\r\n");
        return BM_ERR_PARAM;
    }
    if (row < 60 || row > 6000) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "row(%d) must be within 60 ~ 6000 \n", row);
        return BM_ERR_PARAM;
    }
    if (col < 8) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "col(%d) must be greater than 8 \n", col);
        return BM_ERR_PARAM;
    }
    if (p < 0.0 || p > 1.0) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "p(%d) must be within 0.0f~1.0f \n", p);
        return BM_ERR_PARAM;
    }
    if (weight_mode_KNN < 0 || weight_mode_KNN > 2) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "weight_mode_KNN(%d) must be within 0~2 \n", p);
        return BM_ERR_PARAM;
    }
    return BM_SUCCESS;
}

// D2_0[m * m_obs + idx_data] = sqeucliden(out, data,  m ,idx_data, dims);
static inline float sqeucliden(
        float       *out,
        const float *data,
        const int    idx_out,
        const int    idx_data,
        const int    n_feats) {
    float psum = 0.0;
    for (int i = 0; i < n_feats; i += 1) {
        float p21  = data[idx_data * n_feats + i] - out[idx_out * n_feats + i];
        psum      += p21 * p21;
    }
    return psum;
}

void _kpp_weight_generator_kmeans(
        const float *data,
        float       *out,
        int         *Shape_Output,
        const int   *Shape_Input,
        const int    dims_Input,
        const int    dims_Output,
        const int    k,
        const int    random_mode) {
    if (random_mode == POISSON_CPP) {
        printf("[INFO] start random_mode: POISSON_CPP. \n");
    } else if (random_mode == MT19937_CPP) {
        printf("[INFO] start random_mode: MT19937_CPP. \n");
    } else if (random_mode == CONST_WEIGHT) {
        printf("[INFO] start random_mode: CONST_WEIGHT. \n");
    } else {
        assert(0);
    }

    int dims = 1;
    if (dims_Input > 1) dims = Shape_Input[dims_Input - 1];
    int shape_cnt = 1;
    for (int i = 0; i < dims_Input; i++) {
        shape_cnt *= Shape_Input[i];
    }
    for (int i = 0; i < dims_Output; i++) {
        Shape_Output[i] = 1;
    }
    int m_obs                     = shape_cnt / dims;
    Shape_Output[dims_Output - 1] = dims;
    Shape_Output[dims_Output - 2] = k;

    std::mt19937 mt(UNIVERSAL_SEED);
    std::poisson_distribution<int> dist_possion(m_obs );
    // std::normal_distribution<float> dist_gaussian(m_obs*1.0,7.0 );
    std::uniform_int_distribution<int> dist_uniform(0, m_obs);
    std::cout << "[Random info]" << mt() << std::endl;
    std::uniform_real_distribution<float> dist_float(0.0, 1.0);
    for (int i = 0; i < k; i++) {
        if (i == 0) {

            if (random_mode == POISSON_CPP ) {
                int                                randint = dist_possion(mt);
                memcpy(out, data + randint * dims, dims * sizeof(float));
            } else if (random_mode == MT19937_CPP) {
                int                                randint = dist_uniform(mt);
                memcpy(out, data + randint * dims, dims * sizeof(float));
            } else if (random_mode == CONST_WEIGHT) {
                int fake_rng_randint = int(m_obs / 2);
                memcpy(out, data + fake_rng_randint * dims, dims * sizeof(float));
            } else {
                assert(0);
            }
        } else {
            // sqeuclidean(init[:i,:], data)
            float *D2_0 = new float[i * m_obs];
            for (int m = 0; m < i; m++) {
                for (int idx_data = 0; idx_data < m_obs; idx_data++) {
                    D2_0[m * m_obs + idx_data] = sqeucliden(out, data, m, idx_data, dims);
                }
            }
            float *D2 = new float[m_obs];
            for (int j = 0; j < m_obs; j++) {
                float min = D2_0[j];
                for (int m = 0; m < i; m++) {
                    min = std::min(D2_0[m * m_obs + j], min);
                }
                D2[j] = min;
            }
            float T_sum = 0.0;
            for (int j = 0; j < m_obs; j++) {
                T_sum += D2[j];
            }
            float *probs = new float[m_obs];
            for (int j = 0; j < m_obs; j++) {
                probs[j] = D2[j] / T_sum;
            }
            float *cumprobs = new float[m_obs];
            cumprobs[0]     = probs[0];
            for (int j = 1; j < m_obs; j++) {
                cumprobs[j] = cumprobs[j - 1] + probs[j];
            }
            // r = rng.uniform()
            float r = 0.0; // dist_float(mt);
            if (random_mode == MT19937_CPP || random_mode == POISSON_CPP) {
                r = dist_float(mt);
            } else if (random_mode == CONST_WEIGHT) {
                // np.min(cumprobs) + (np.max(cumprobs)- np.min(cumprobs))/2
                float max_temp = cumprobs[0];
                float min_temp = cumprobs[0];
                for (int idx = 1; idx < m_obs; idx++) {
                    min_temp = cumprobs[idx] > min_temp ? min_temp : cumprobs[idx];
                    max_temp = cumprobs[idx] > max_temp ? cumprobs[idx] : max_temp;
                }
                r = min_temp + (max_temp - min_temp) / 2.0; // 0.5072551558477147;
            } else {
                assert(0);
            }
            int sort_idx = 0;
            for (int j = 1; j < m_obs; j++) {
                if ((cumprobs[j - 1] < r) && (r <= cumprobs[j])) {
                    sort_idx = j;
                    break;
                }
            }
            memcpy(out + i * dims, data + sort_idx * dims, dims * sizeof(float));
            delete[] D2;
            delete[] D2_0;
            delete[] probs;
            delete[] cumprobs;
        }
    }
}

bm_status_t bmcv_cluster(bm_handle_t     handle,
                         bm_device_mem_t input,
                         bm_device_mem_t output,
                         int             row,
                         int             col,
                         float           p,
                         int             min_num_spks,
                         int             max_num_spks,
                         int             user_num_spks,
                         int             weight_mode_KNN,
                         int             num_iter_KNN
) {
    bm_status_t    ret    = BM_SUCCESS;
    unsigned int   chipid = 0;
    struct timeval start, end;

    float *fake_weight     = (float *)malloc(max_num_spks * max_num_spks * sizeof(float));

    float* arm_select_egin_vector = (float *)malloc(row * max_num_spks * sizeof(float));
    int*   num_spks_output        = (int *)malloc(1 * sizeof(int));
    float* input_A                = (float *)malloc(row * row * sizeof(float));
    float* arm_sorted_eign_value  = (float *)malloc(row * sizeof(float));
    int    Shape_Weight_fake[8];

    int m_obs               = row;
    int num_spks_practical  = max_num_spks;
    int n_feat              = num_spks_practical;
    int k                   = n_feat;
    int m_code              = num_spks_practical;
    int buffer_coeff_KNN    = 3;
    int buffer_max_cnt      = n_feat * row;
    int Shape_Input_KNN[4]  = {1, 1, m_obs, n_feat};
    int Shape_Weight_KNN[4] = {1, 1, m_code, n_feat};
    int dims_Input_KNN      = 4;
    int dims_Weight_KNN     = 4;

    unsigned int shape_cnt_mobs_nfeat = 1;
    for (int i = 0; i < dims_Input_KNN; i++)
    {
        shape_cnt_mobs_nfeat *= Shape_Input_KNN[i];
    }
    unsigned int shape_cnt_mcode_nfeat = 1;
    for (int i = 0; i < dims_Weight_KNN; i++)
    {
        shape_cnt_mcode_nfeat *= Shape_Weight_KNN[i];
    }
    unsigned int shape_labels = shape_cnt_mobs_nfeat/n_feat;

    std::ifstream file_weight_scipy("/mnt/software/code_eigh/KNN_weight.bin", std::ios::in | std::ios::binary);
    std::ifstream file_data("/mnt/software/code_eigh/KNN_input_data.bin", std::ios::in | std::ios::binary);

    bm_device_mem_t input_data;
    bm_device_mem_t normalized_data;
    bm_device_mem_t cos_output_data;
    bm_device_mem_t prune_output_data;
    bm_device_mem_t sort_index;
    bm_device_mem_t lap_output_data;
    bm_device_mem_t spectral_embeddings;
    bm_device_mem_t buffer_tpu;
    bm_device_mem_t centroids;
    bm_device_mem_t labels;
    bm_device_mem_t weight;

    ret = bmcv_cluster_check(handle, row, col, p, weight_mode_KNN);
    if (BM_SUCCESS != ret) {
        return ret;
    }
    ret = bm_get_chipid(handle, &chipid);
    if (ret != BM_SUCCESS || chipid != BM1684X) {
        printf("chipid (0x%x) != 0x1686\n", chipid);
        return BM_ERR_FAILURE;
    }

    if (bm_mem_get_type(input) == BM_MEM_TYPE_SYSTEM) {
        ret = bm_malloc_device_byte(handle, &input_data, sizeof(float) * row * col);
        if (ret != BM_SUCCESS) {
            bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte input_data error\n");
            goto err0;
        }
        ret = bm_memcpy_s2d(handle, input_data, bm_mem_get_system_addr(input));
        if (ret != BM_SUCCESS) {
            bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_memcpy_s2d error\n");
            goto err111;
        }
    } else {
        input_data = input;
    }

    if (bm_mem_get_type(output) == BM_MEM_TYPE_SYSTEM) {
        ret = bm_malloc_device_byte(handle, &labels, sizeof(int) * shape_labels);
        if (ret != BM_SUCCESS) {
            bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte labels error\n");
            goto err1;
        }
    } else {
        labels = output;
    }

    ret = bm_malloc_device_byte(handle, &normalized_data, sizeof(float) * row * col);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte normalized_data error\n");
        goto err2;
    }
    ret = bm_malloc_device_byte(handle, &cos_output_data, sizeof(float) * row * row);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte cos_output_data error\n");
        goto err3;
    }
    ret = bm_malloc_device_byte(handle, &prune_output_data, sizeof(float) * row * row);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte prune_output_data error\n");
        goto err4;
    }
    ret = bm_malloc_device_byte(handle, &sort_index, sizeof(float) * row * row);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte sort_index error\n");
        goto err5;
    }
    ret = bm_malloc_device_byte(handle, &lap_output_data, sizeof(float) * row * row);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte lap_output_data error\n");
        goto err6;
    }
    ret = bm_malloc_device_byte(handle, &spectral_embeddings, sizeof(float) * shape_cnt_mobs_nfeat);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte spectral_embeddings error\n");
        goto err7;
    }
    ret = bm_malloc_device_byte(handle, &buffer_tpu, sizeof(float) * buffer_coeff_KNN * buffer_max_cnt);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte buffer_tpu error\n");
        goto err12;
    }
    ret = bm_malloc_device_byte(handle, &centroids, sizeof(float) * shape_cnt_mobs_nfeat);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte centroids error\n");
        goto err13;
    }
    ret = bm_malloc_device_byte(handle, &weight, sizeof(float) * shape_cnt_mcode_nfeat);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_malloc_device_byte weight error\n");
        goto err14;
    }

    ret = bm_set_sync_timeout(handle, 20000000);

    gettimeofday(&start, NULL);
    ret = bmcv_cos_similarity(handle,
                              input_data,
                              normalized_data,
                              cos_output_data,
                              row,
                              col);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bmcv_cos_similarity error\n");
        goto err14;
    }
    gettimeofday(&end, NULL);
    printf("[Stage-1]cos_similarity TPU using time: %.4f(s)\n", TIME_COST_S(start, end));

    gettimeofday(&start, NULL);
    ret = bmcv_matrix_prune(handle,
                            cos_output_data,
                            prune_output_data,
                            sort_index,
                            row,
                            p);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bmcv_matrix_prune error\n");
        goto err14;
    }
    gettimeofday(&end, NULL);
    printf("[Stage-2]matrix_prune TPU using time: %.4f(s)\n", TIME_COST_S(start, end));

    gettimeofday(&start, NULL);
    ret = bmcv_lap_matrix(handle,
                          prune_output_data,
                          lap_output_data,
                          row,
                          row);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bmcv_lap_matrix error\n");
        goto err14;
    }
    gettimeofday(&end, NULL);
    printf("[Stage-3]lap_matrix TPU using time: %.4f(s)\n", TIME_COST_S(start, end));

    gettimeofday(&start, NULL);
    ret = bm_memcpy_d2s(handle, input_A, lap_output_data);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_memcpy_d2s input_A error\n");
        goto err14;
    }

    bmcv_qr_cpu(
        arm_select_egin_vector,
        num_spks_output,
        input_A,
        arm_sorted_eign_value,
        row,
        user_num_spks,
        max_num_spks,
        min_num_spks);
    if(USING_REF)
        file_data.read((char*)arm_select_egin_vector,                   row  * num_spks_output[0] * sizeof(float));
    ret = bm_memcpy_s2d(handle, spectral_embeddings, arm_select_egin_vector);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_memcpy_s2d spectral_embeddings error\n");
        goto err7;
    }

    gettimeofday(&end, NULL);
    printf("[Stage-4]qr external CPU using time: %.4f(s)\n", TIME_COST_S(start, end));
    printf("[Stage-4-Info]num_spks_output: %d\n", num_spks_output[0]);

    num_spks_practical  = num_spks_output[0];
    n_feat              = num_spks_practical;
    k                   = n_feat;
    m_code              = num_spks_practical;
    buffer_max_cnt      = n_feat * row;
    Shape_Input_KNN[3]  = n_feat;
    Shape_Weight_KNN[2] = m_code;
    Shape_Weight_KNN[3] = n_feat;

    _kpp_weight_generator_kmeans(arm_select_egin_vector,
                                 fake_weight,
                                 Shape_Weight_fake,
                                 Shape_Input_KNN,
                                 dims_Input_KNN,
                                 dims_Weight_KNN,
                                 k,
                                 weight_mode_KNN);
    if(USING_REF)
        file_weight_scipy.read((char*)fake_weight,                   num_spks_output[0] * num_spks_output[0] * sizeof(float));

    ret = bm_memcpy_s2d(handle, weight, fake_weight);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_memcpy_s2d weight error\n");
        goto err14;
    }

    gettimeofday(&start, NULL);
    ret = bmcv_knn(handle,
                   // output
                   centroids, //[m_obs,  n_feat]
                   labels,    //[m_obs]
                   // input nxn
                   spectral_embeddings, //[m_obs,   n_feat]
                   weight,              //[c]
                   // buffer 2 * max(m_obs, m_code) * 4
                   buffer_tpu,
                   Shape_Input_KNN,
                   Shape_Weight_KNN,
                   dims_Input_KNN,
                   dims_Weight_KNN,
                   n_feat,
                   k,
                   num_iter_KNN,
                   buffer_coeff_KNN,
                   buffer_max_cnt,
                   DT_FP32);
    if (ret != BM_SUCCESS) {
        bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bmcv_knn error\n");
        goto err14;
    }
    gettimeofday(&end, NULL);
    printf("[Stage-5]knn TPU using time: %.4f(s)\n", TIME_COST_S(start, end));

    if (bm_mem_get_type(output) == BM_MEM_TYPE_SYSTEM) {
        ret = bm_memcpy_d2s(handle, bm_mem_get_system_addr(output), labels);
        if (ret != BM_SUCCESS) {
            bmlib_log("CLUSTER", BMLIB_LOG_ERROR, "bm_memcpy_d2s output error\n");
            goto err14;
        }
    }

err14:
    bm_free_device(handle, weight);
err13:
    bm_free_device(handle, centroids);
err12:
    bm_free_device(handle, buffer_tpu);
err7:
    bm_free_device(handle, spectral_embeddings);
err6:
    bm_free_device(handle, lap_output_data);
err5:
    bm_free_device(handle, sort_index);
err4:
    bm_free_device(handle, prune_output_data);
err3:
    bm_free_device(handle, cos_output_data);
err2:
    bm_free_device(handle, normalized_data);
err1:
    if (bm_mem_get_type(output) == BM_MEM_TYPE_SYSTEM) {
        bm_free_device(handle, labels);
    }
err111:
    if (bm_mem_get_type(input) == BM_MEM_TYPE_SYSTEM) {
        bm_free_device(handle, input_data);
    }
err0:
    return ret;
}
#endif
