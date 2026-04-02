#include "ai_person.h"

#include "person_detect_model_data.h"
#include "model_settings.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// THÊM THƯ VIỆN NÀY ĐỂ GỌI PSRAM
#include "esp_heap_caps.h" 

#define TENSOR_ARENA_SIZE (140 * 1024)

// THAY ĐỔI: Chỉ khai báo con trỏ, không cấp phát cứng nữa
static uint8_t *tensor_arena = nullptr;

static tflite::MicroInterpreter *interpreter;
static TfLiteTensor *input;
static TfLiteTensor *output;

extern "C" void ai_init(void)
{
    // THAY ĐỔI: Xin thẳng 140KB từ PSRAM (RAM ngoài)
    if (tensor_arena == nullptr) {
        tensor_arena = (uint8_t *)heap_caps_aligned_alloc(16, TENSOR_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (tensor_arena == nullptr) {
            printf("\n[LOI] Khong the cap phat PSRAM cho AI!\n");
            return;
        }
    }

    const tflite::Model *model = tflite::GetModel(g_person_detect_model_data);

    static tflite::MicroMutableOpResolver<6> micro_op_resolver;

    micro_op_resolver.AddAveragePool2D();
    micro_op_resolver.AddConv2D();
    micro_op_resolver.AddDepthwiseConv2D();
    micro_op_resolver.AddReshape();
    micro_op_resolver.AddSoftmax();
    micro_op_resolver.AddFullyConnected();

    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, TENSOR_ARENA_SIZE);

    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        printf("\n[LOI NGHIEM TRONG] AllocateTensors failed! Thieu RAM cho AI!\n\n");
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);

    printf("AI init done (using PSRAM)\n");
}

extern "C" int ai_run(uint8_t *image_data)
{
    if (input == nullptr || interpreter == nullptr) {
        printf("Loi: AI chua duoc khoi tao (ai_init)!\n");
        return -1; 
    }

    // copy + normalize
    for (int i = 0; i < 96 * 96; i++)
    {
        input->data.int8[i] = image_data[i] - 128;
    }

    if (interpreter->Invoke() != kTfLiteOk)
    {
        printf("Invoke failed\n");
        return -1;
    }

    int person_score = output->data.int8[1];
    int no_person_score = output->data.int8[0];

    printf("Person: %d | No: %d\n", person_score, no_person_score);
    // cho person_score vào json để hiển thị trên dashboard
    return (person_score > no_person_score);
}