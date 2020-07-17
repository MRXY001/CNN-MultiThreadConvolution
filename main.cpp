#include <stdio.h>
#include <pthread.h>
#include <algorithm>
#include <queue>
#include <windows.h>

#define MAP_SIDE_MAX 224    // ͼ�߳������
#define MAP_CHANNEL_DEFULT 3       // ͼͨ��Ĭ������ RGB
#define KERNEL_SIDE 3       // ����˹̶��߳�
#define KERNEL_MAX_COUNT 32 // ������������
#define MAX_LAYER 32        // ���Ĳ�����32����128��

struct FeatureMap;

//int layer_count[KERNEL_MAX_COUNT]; // ÿһ�����ھ����������������ͬʱ���в����õ�
//int thread_finished[KERNEL_MAX_COUNT]; // ÿ������߳��Ƿ���������0=δ����, 1=������, -1=�ѽ���
int current_layer = 0;   // ��ǰ�����ڵڼ���
int finished_kernel = 0; // ��ǰ�������kernel����
std::vector<FeatureMap*> feature_maps; // ÿ��ͼ

INT8*** create3D(int y, int x, int z);

/**
 * ͼ��
 * �����˱�ź�ͼ�Ĳ���
 */
struct FeatureMap {
    FeatureMap(){}
    FeatureMap(int k, int side, int channel) : FeatureMap(side, channel)
    {
        this->kernel = k;
    }
    FeatureMap(int side, int channel) : side(side), channel(channel)
    {
        initMap();
    }
    FeatureMap(int k, FeatureMap *map)
    {
        this->kernel = k;
        this->side = map->side;
        this->channel = map->channel;
        initMap();
//        printf("initMap finished: %d, %d, %d  %d~%d\n", side, side, channel, this->map[side-1][side-1][channel-1], map->map[side-1][side-1][channel-1]);
//        memcpy(this->map, map->map, sizeof(INT8)*side*side*channel); // Ī���ı���
//        printf("memcpy finished\n");
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                for (int z = 0; z < channel; z++)
                    this->map[y][x][z] = map->map[y][x][z];
    }
    ~FeatureMap()
    {
        if (map)
        {
            for (int y = 0; y < side; y++)
            {
                for (int x = 0; x < side; x++)
                {
                    delete[] map[y][x];
                }
                delete map[y];
            }
            delete[] map;
        }
    }

    int kernel = 0;     // kernel ��š�������ͼ����Ҫ����
    int side = 0;       // ͼ�ı߳��������Σ�
    int channel = 0;    // ͼ��channel����
    INT8 ***map = NULL; // ͼ��Ϊ�˱������㣬Ϊ��map[channel][side][side]

    /**
     * ��ʼ��ͼ��ȫ����Ĭ��0
     * @param m ��������ͼ�����ΪNULL��ȫ�����ó�0
     */
    void initMap(INT8***m = NULL)
    {
        map = m ? m : create3D(side, side, channel);
    }
};

/**
 * �������
 */
struct Kernel {
    Kernel(): side(3), channel(3) {}
    Kernel(int side, int channel)
        : side(side), channel(channel)
    {
        initKernel();
    }
    int side;    // �߳� side * side
    int channel; // ���ڵ�ǰ��������ͼ��channel����
    INT8 ***bits = NULL; // ÿһλ��ֵ

    /**
     * ��ʼ��kernel
     * @param k ���ΪNULL����ȫ��Ϊ0
     */
    void initKernel(INT8*** k = NULL)
    {
        bits = k ? k : create3D(side, side, channel);
    }
};

/**
 * �̴߳��ݲ�����
 * kernel.channel == image.channel
 * kernel.filter = ��һ�� image.channel
 */
struct ConvThreadArg {
    ConvThreadArg(){}
    ConvThreadArg(int layer, int k, FeatureMap *img, Kernel *kernel)
        : layer(layer), k_indx(k), map(img), kernel(kernel)
    {}
    int layer = 0;          // ��ǰ�ǵڼ���
    int k_indx = 0;         // �˵�������������һ����
    FeatureMap *map = NULL; // ͼ�Ķ���ָ��
    Kernel *kernel;         // ����˵ı߳�
};

/**
 * ��ȡ����˵�����
 * ��ʵÿһ������������һ�������
 * 3*3*3 -> 3*3*8 -> 16 -> 32 -> 32 -> 32...
 */
inline int getKernelCount(int layer)
{
    switch (layer)
    {
    case 0:
        return 3;
    case 1:
        return 8;
    case 2:
        return 16;
    default:
        return 32;
    }
}

/**
 * ���о���ļ��㺯��
 */
FeatureMap* convolution(FeatureMap *image, Kernel *kernel)
{
    int new_side = image->side - kernel->side + 1;
    FeatureMap* result = new FeatureMap(image->kernel, new_side, 1);
    INT8*** map = result->map;

    // �ۼӣ�ע�⣺�������귴�ŵģ�����y������x��
    for (int y = 0; y < new_side; y++)
    {
        for (int x = 0; x < new_side; x++)
        {
            // ��ͼ��λ�ã�map[y][x][ch]
            // TODO��������ԼӸ��������ӿ��ٶ�
            INT8& v = map[y][x][0];
            for (int i = 0; i < kernel->side; i++)
                for (int j = 0; j < kernel->side; j++)
                    for (int k = 0; k < kernel->channel; k++)
                        v += image->map[i][j][k];
        }
    }
    return result;
}

/**
 * ��������˽��о�����߳�
 * @return ���������chnnel��ͼ���ϲ���1��
 */
void *convolutionThread(void *_arg)
{
    pthread_detach(pthread_self()); // unjoinable�������������н������˳�
    ConvThreadArg* arg = (ConvThreadArg*) _arg;
    FeatureMap* map = arg->map;
    Kernel* kernel = arg->kernel;
    printf("> ��ʼ������߳�: kernel: %d\n", arg->k_indx);
    if (map)
        printf("    ����ͼ: %d * %d * %d\n", map->side, map->side, map->channel);
    if (kernel)
        printf("    �����: %d * %d * %d\n", kernel->side, kernel->side, kernel->channel);

    // ��ʼ���
    FeatureMap *result = convolution(map, kernel);

    // ���Ҫ���ݵ���һ��
    feature_maps.push_back(result);
    finished_kernel++;

    printf("- �������: kernel: %d\n", arg->k_indx);
    // �ͷ���Դ
    delete arg;
    delete map;
    pthread_exit(0);
    return 0;
}

int main()
{
    // �����߳�
    std::vector<pthread_t*> conv_thread;

    // ���δ���ͨ��
    current_layer = 0;   // ��0��
    finished_kernel = 3; // ��0���kernel��=��1���channel=3
    feature_maps.push_back(new FeatureMap(0, MAP_SIDE_MAX, MAP_CHANNEL_DEFULT)); // Ĭ��224*224*3��ͼ

    // ��ѭ��һֱ�ȵ�picker
    while (true)
    {
        Sleep(1); // ����ֱ�ӿ���
        int kernel_count = getKernelCount(current_layer); // ��һ���kernel����
        // ����ȷ�� finished_kernel == kernel_count == feature_maps.count(), �� > 0
        if (finished_kernel < kernel_count)
            continue;

        // �ϲ�FeatureMap
        FeatureMap* map = NULL;
        int channel_count = kernel_count; // ��һ���channel���� = ��һ���kernel���� = ��һ������map������
        if (current_layer <= 0) // ����ʹ�ã�������ô��Ĳ���
        {
            map = feature_maps.front();
            feature_maps.clear(); // map����Ҫ�õ�������delete
            printf("��ʼ����ͼ��%d * %d * %d\n", map->side, map->side, map->channel);
        }
        else
        {
            // �ϲ���һ��ÿ��kernel��FeatureMap
            std::vector<FeatureMap*> prev_map = feature_maps;
            // �����߳�kernel˳�������������ϲ�
            std::sort(prev_map.begin(), prev_map.end(), [=](FeatureMap* a, FeatureMap* b){
                return a->kernel < b->kernel;
            });

            int side = prev_map.front()->side;
            map = new FeatureMap(0, side, channel_count);
            printf("�ϲ�����ͼ��%d * %d * %d\n", side, side, channel_count);
            for (int i = 0; i < channel_count; i++)
            {
                // memcpy(map->map[i], prev_map.at(i)->map[0], sizeof(INT8)*side*side); // ���������ڴ棬�޷�cpy
                INT8*** p_map = prev_map.at(i)->map;
                for (int y = 0; y < side; y++)
                {
                    for (int x = 0; x < side; x++)
                    {
                        map->map[y][x][i] = p_map[y][x][0];
                    }
                }
            }

            // �ͷ���һ��ָ����
            while (!feature_maps.empty())
            {
                delete feature_maps.back();
                feature_maps.pop_back();
            }

            // �ͷ���һ����߳�
            while (!conv_thread.empty())
            {
                pthread_join(*conv_thread.back(), NULL);
                conv_thread.pop_back();
            }
        }

        // ��� MAX_LAYER ��(Ŀǰ32)
        if (current_layer >= MAX_LAYER)
        {
            // �����������ĵ�����ͼmap�С���ʱû�����
            printf("ȫ�����н���");
            break;
        }
        // ������һ��
        current_layer++;
        printf("\n================ �����%d�� ================\n\n", current_layer);
        kernel_count = getKernelCount(current_layer); // ��ǰ���kernel����
        printf("kernel count = %d\n", kernel_count);
        finished_kernel = 0; // ����ɵ��߳���������Ϊ0
        for (int k = 0; k < kernel_count; k++)
        {
            // ����ȫ�����ݣ�ͼ+�ˣ������߳̽�����ʱ��delete��
            ConvThreadArg* arg = new ConvThreadArg(current_layer, k+1, new FeatureMap(k+1, map), new Kernel(KERNEL_SIDE, channel_count));

            // ������̡߳��ò����߳�ȫ��������ͳһ�ͷ�
            pthread_t* thread = new pthread_t;
            conv_thread.push_back(thread);
            int ret = pthread_create(thread, NULL, convolutionThread, (void*)arg);
            if (ret != 0)
                printf("pthread_create error: %d\n", ret);
        }
    }

    pthread_exit(NULL);
    return 0;
}

/**
 * �����̶���С����ά����
 * �ǵ��ֶ�delete[]������ڴ�й©
 * ���Ҫ�Ż��ٶȣ����԰�Z���������ѭ��
 */
INT8*** create3D(int y, int x, int z)
{
    INT8*** bits = new INT8**[y];
    for (int i = 0; i < y; i++)
    {
        bits[i] = new INT8*[x];
        for (int j = 0; j < x; j++)
        {
            bits[i][j] = new INT8[z];
            for (int k = 0; k < z; k++)
                bits[i][j][k] = 0;
        }
    }
    return bits;
}
