#include <iostream>
#include "bmcv_api_ext.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
// #include "md5.h"
#include "test_misc.h"
#include <assert.h>
#include <vector>
#ifdef __linux__
#include <sys/time.h>
#else
#include <windows.h>
#include "time.h"
#endif

typedef unsigned long long u64;
using namespace std;

static int g_size = 0;

using namespace std;

template <typename T>
void fill(
    T* input,
    int size,
    int flag) {
    switch(flag) {
        case 1: {
            T count = static_cast<T>(10);
            for (int i = 0; i < size; i++) {
                input[i] = count;
                count++;
                if (count == static_cast<T>(256)) {
                    count = static_cast<T>(10);
                }
            }
            break;
        }
        case 2: {
            T count = static_cast<T>(1);
            for (int i = 0; i < size; i++) {
                input[i] = count;
                count++;
                if (count == static_cast<T>(9)) {
                    count = static_cast<T>(1);
                }
            }
            break;
        }
        default:
            break;
    }
}

static void readBin(const char* path, unsigned char* input_data, int size)
{
    FILE *fp_src = fopen(path, "rb");
    if (fread((void *)input_data, 1, size, fp_src) < (unsigned int)size) {
        printf("file size is less than %d required bytes\n", size);
    };

    fclose(fp_src);
}

template <typename T>
static void write_bin(const char *output_path, T *output_data, int size)
{
    FILE *fp_dst = fopen(output_path, "wb");

    if (fp_dst == NULL) {
        printf("unable to open output file %s\n", output_path);
        return;
    }

    if(fwrite(output_data, sizeof(T), size, fp_dst) != 0) {
        printf("write image success\n");
    }
    fclose(fp_dst);
}

template <typename T>
int add_weighted_tpu(
        T* input1,
        T* input2,
        T* output,
        int height,
        int width,
        int format,
        int data_type,
        float alpha,
        float beta,
        float gamma) {
    bm_handle_t handle;
    bm_status_t ret = bm_dev_request(&handle, 0);
    if (ret != BM_SUCCESS) {
        printf("Create bm handle failed. ret = %d\n", ret);
        return -1;
    }
    bm_image input1_img;
    bm_image input2_img;
    bm_image output_img;
    bm_image_create(handle, height, width, (bm_image_format_ext)format, (bm_image_data_format_ext)data_type, &input1_img);
    bm_image_create(handle, height, width, (bm_image_format_ext)format, (bm_image_data_format_ext)data_type, &input2_img);
    bm_image_create(handle, height, width, (bm_image_format_ext)format, (bm_image_data_format_ext)data_type, &output_img);
    bm_image_alloc_dev_mem(input1_img);
    bm_image_alloc_dev_mem(input2_img);
    bm_image_alloc_dev_mem(output_img);

    int image_byte_size[4] = {0};
    bm_image_get_byte_size(input1_img, image_byte_size);

    // int byte_size  = image_byte_size[0] + image_byte_size[1] + image_byte_size[2] + image_byte_size[3];
    void* input_addr1[4] = {(void *)input1,
                        (void *)((T*)input1 + image_byte_size[0]/sizeof(T)),
                        (void *)((T*)input1 + (image_byte_size[0] + image_byte_size[1])/sizeof(T)),
                        (void *)((T*)input1 + (image_byte_size[0] + image_byte_size[1] + image_byte_size[2])/sizeof(T))};
    void* input_addr2[4] = {(void *)input2,
                        (void *)((T*)input2 + image_byte_size[0]/sizeof(T)),
                        (void *)((T*)input2 + (image_byte_size[0] + image_byte_size[1])/sizeof(T)),
                        (void *)((T*)input2 + (image_byte_size[0] + image_byte_size[1] + image_byte_size[2])/sizeof(T))};
    void* out_addr[4] = {(void *)output,
                        (void *)((T*)output + image_byte_size[0]/sizeof(T)),
                        (void *)((T*)output + (image_byte_size[0] + image_byte_size[1])/sizeof(T)),
                        (void *)((T*)output + (image_byte_size[0] + image_byte_size[1] + image_byte_size[2])/sizeof(T))};

    bm_image_copy_host_to_device(input1_img, (void **)input_addr1);
    bm_image_copy_host_to_device(input2_img, (void **)input_addr2);

    ret = bmcv_image_add_weighted(handle, input1_img, alpha, input2_img, beta, gamma, output_img);

    bm_image_copy_device_to_host(output_img, (void **)out_addr);
    bm_image_destroy(input1_img);
    bm_image_destroy(input2_img);
    bm_image_destroy(output_img);
    bm_dev_free(handle);
    return ret;
}

static int get_image_size(int format, int width, int height) {
    int size = 0;
    switch (format)
    {
    case FORMAT_YUV420P:
    case FORMAT_NV12:
    case FORMAT_NV21:
        size = width * height * 3 / 2;
        break;
    case FORMAT_YUV422P:
    case FORMAT_NV16:
    case FORMAT_NV61:
    case FORMAT_NV24:
        size = width * height * 2;
        break;
    case FORMAT_YUV444P:
    case FORMAT_RGB_PLANAR:
    case FORMAT_BGR_PLANAR:
    case FORMAT_RGB_PACKED:
    case FORMAT_BGR_PACKED:
    case FORMAT_RGBP_SEPARATE:
    case FORMAT_BGRP_SEPARATE:
        size = width * height * 3;
        break;
    case FORMAT_GRAY:
        size = width * height;
        break;
    default:
        break;
    }
    return size;
}

static int test_add_weighted_random(
        int height,
        int width,
        int format,
        int data_type,
        int real_img) {
    int ret;
    float alpha = 0.773;
    float beta = 0.22;
    float gamma = 41.6;

    char input_path1[256];
    char input_path2[256];
    char output_path[256];
    snprintf(input_path1, sizeof(input_path1), "path/to/input1_%d_%d_%d_%d.bin", width, height, data_type, format);
    snprintf(input_path2, sizeof(input_path2), "path/to/input2_%d_%d_%d_%d.bin", width, height, data_type, format);
    snprintf(output_path, sizeof(output_path), "path/to/output_%d_%d_%d_%d.bin", width, height, data_type, format);


    cout << "format: " << format << endl;
    cout << "data_type: " << data_type << endl;
    cout << "width: " << width << "  height: " << height << endl;
    cout << "alpha: " << alpha << "  beta: " << beta << "  gamma: " << gamma << endl;

    // int size = 0;
    g_size = get_image_size(format, width, height);

    unsigned char* input1_data_uchar = (unsigned char*)malloc(g_size * sizeof(unsigned char));
    unsigned char* input2_data_uchar = (unsigned char*)malloc(g_size * sizeof(unsigned char));
    unsigned char* output_tpu_uchar = (unsigned char*)malloc(g_size * sizeof(unsigned char));

    float* input1_data_float = (float*)malloc(g_size * sizeof(float));
    float* input2_data_float = (float*)malloc(g_size * sizeof(float));
    float* output_tpu_float = (float*)malloc(g_size * sizeof(float));

    if (real_img == 1) {

        readBin(input_path1, input1_data_uchar, g_size);
        readBin(input_path2, input2_data_uchar, g_size);
        if (data_type == 0) {
            for (int i = 0; i < g_size; i++) {
                input1_data_float[i] = (float)input1_data_uchar[i];
                input2_data_float[i] = (float)input2_data_uchar[i];
            }
            ret = add_weighted_tpu(input1_data_float, input2_data_float, output_tpu_float, height, width, format, data_type, alpha, beta, gamma);
            write_bin(output_path, output_tpu_float, g_size);
        } else {
            ret = add_weighted_tpu(input1_data_uchar, input2_data_uchar, output_tpu_uchar, height, width, format, data_type, alpha, beta, gamma);
            write_bin(output_path, output_tpu_uchar, g_size);
        }
    } else {
        if (data_type == 0) {
            fill(input1_data_float, g_size, 1);
            write_bin(input_path1, input1_data_float, g_size);
            fill(input2_data_float, g_size, 2);

            write_bin(input_path2, input2_data_float, g_size);
            fill(output_tpu_float, g_size, 2);
            ret = add_weighted_tpu(input1_data_float, input2_data_float, output_tpu_float, height, width, format, data_type, alpha, beta, gamma);
            write_bin(output_path, output_tpu_float, g_size);

            for (int i = 0; i < g_size; i++) {
                output_tpu_uchar[i] = (unsigned char)output_tpu_float[i];
            }
        } else {
            fill(input1_data_uchar, g_size, 1);
            write_bin(input_path1, input1_data_uchar, g_size);
            fill(input2_data_uchar, g_size, 2);
            write_bin(input_path2, input2_data_uchar, g_size);
            fill(output_tpu_uchar, g_size, 2);
            ret = add_weighted_tpu(input1_data_uchar, input2_data_uchar, output_tpu_uchar, height, width, format, data_type, alpha, beta, gamma);
            write_bin(output_path, output_tpu_uchar, g_size);
        }
    }

    free(output_tpu_float);
    free(input1_data_uchar);
    free(input2_data_uchar);
    free(output_tpu_uchar);
    return ret;
}

int main(int argc, char* args[]) {
    int loop = 1;
    int width = 800;
    int height = 600;
    int format = 8;
    int data_type = 0;  // float: 0; unsigned char: 1
    int real_img = 0;
    if (argc > 1) loop = atoi(args[1]);
    if (argc > 2) width = atoi(args[2]);
    if (argc > 3) height = atoi(args[3]);
    if (argc > 4) format = atoi(args[4]);
    if (argc > 5) data_type = atoi(args[5]);
    if (argc > 6) real_img = atoi(args[6]);

    if (argc == 2 && atoi(args[1]) == -1) {
        printf("usage:\n");
        printf("%s loop width height format data_type real_img \n", args[0]);
        printf("example:\n");
        printf("%s \n", args[0]);
        printf("%s 2\n", args[0]);
        printf("%s 1 512 512 8 0 0\n", args[0]);
        return 0;
    }

    int ret = 0;
    for (int i = 0; i < loop; i++) {
        ret = test_add_weighted_random(height, width, format, data_type, real_img);

        if (ret) {
            cout << "test add_weighted failed" << endl;
            return ret;
        }
    }
    cout << "test add_weighted successfully!" << endl;
    return 0;
}