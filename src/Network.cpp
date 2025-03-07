#include <iostream>
#include <string.h>

#include "tkdnn.h"
#include "Network.h"
#include "Layer.h"

namespace tk { namespace dnn {

Network::Network(dataDim_t input_dim, bool letter_box) {
    this->input_dim = input_dim;

    float tk_ver = float(TKDNN_VERSION)/1000;
    float cu_ver = float(cudnnGetVersion())/1000;

    std::cout<<"New NETWORK (tkDNN v"<<tk_ver
             <<", CUDNN v"<<cu_ver<<")\n";
    dataType = CUDNN_DATA_FLOAT;
    tensorFormat = CUDNN_TENSOR_NCHW;
    dontLoadWeights = false;
    num_layers = 0;
	num_calib_images = 100;
	letterBox = letter_box;

    fp16 = true;
    dla = false;
    int8 = false;

    maxBatchSize = 1;
    if(const char* env_p = std::getenv("TKDNN_BATCHSIZE")) {
        maxBatchSize = atoi(env_p);
    }
    if(const char* env_p = std::getenv("TKDNN_CALIB_IMG_PATH"))
        fileImgList = env_p;
    
    if(const char* env_p = std::getenv("TKDNN_CALIB_LABEL_PATH"))
        fileLabelList = env_p;
    
   
    if(fp16)
        std::cout<<COL_REDB<<"!! FP16 INFERENCE ENABLED !!"<<COL_END<<"\n";
    if(dla)
        std::cout<<COL_GREENB<<"!! DLA INFERENCE ENABLED !!"<<COL_END<<"\n";
    if(int8)
        std::cout<<COL_ORANGEB<<"!! INT8 INFERENCE ENABLED !!"<<COL_END<<"\n";


    checkCUDNN( cudnnCreate(&cudnnHandle) );
    checkERROR( cublasCreate(&cublasHandle) );

}

Network::~Network() {
    checkCUDNN( cudnnDestroy(cudnnHandle) );
    checkERROR( cublasDestroy(cublasHandle) );
}

void Network::releaseLayers() {
    for(int i=0; i<num_layers; i++)
        delete layers[i];
    num_layers = 0;
}

dnnType* Network::infer(dataDim_t &dim, dnnType* data) {

    //do infer for every layer
    for(int i=0; i<num_layers; i++) {
        data = layers[i]->infer(dim, data);
    }
    checkCuda(cudaDeviceSynchronize());
    return data;
}

bool Network::addLayer(Layer *l) {
    if(num_layers == MAX_LAYERS)
        return false;
    
    l->id = num_layers;
    layers[num_layers++] = l;
    return true;
}

dataDim_t Network::getOutputDim() {

        if(num_layers == 0)
            return input_dim;
        else
            return layers[num_layers-1]->output_dim;
}

void Network::adjustFeatureMapSizeWithShortcuts(){
    layerType_t layer_type;
    int shortcutted_idx;

    for(int i=0; i<num_layers; i++) {
        layer_type = layers[i]->getLayerType();
        if(layer_type == LAYER_SHORTCUT){
            shortcutted_idx = -1;
            for(int j=0; j<num_layers; j++) {
                if(static_cast<tk::dnn::Shortcut*>(layers[i])->backLayer == layers[j]){
                    shortcutted_idx = j;
                    break;
                }
            }
            if(shortcutted_idx == -1)
                FatalError("Problem when computing featuer_map_size with shortcuts");
            for(int j=shortcutted_idx+1; j<i; ++j)
                layers[j]->feature_map_size += layers[shortcutted_idx]->output_dim.tot();
        }
    }
}

void Network::print() {

    printCenteredTitle(" NETWORK MODEL ", '=', 60);
    std::cout.width(3); std::cout<<std::left<<"N.";
    std::cout<<" ";
    std::cout.width(17); std::cout<<std::left<<"Layer type";
    std::cout.width(22); std::cout<<std::left<<"input (H*W,CH)";
    std::cout.width(16); std::cout<<std::left<<"output (H*W,CH)";
    std::cout<<"\n";

    adjustFeatureMapSizeWithShortcuts();
    
    long long unsigned int tot_params = 0;
    long long unsigned int max_feature_map_size = 0;
    long long unsigned int tot_MACC = 0;
    
    for(int i=0; i<num_layers; i++) {
        dataDim_t in = layers[i]->input_dim;
        dataDim_t out = layers[i]->output_dim;

        tot_params += layers[i]->n_params;
        tot_MACC += layers[i]->MACC;
        if(layers[i]->feature_map_size> max_feature_map_size)
            max_feature_map_size = layers[i]->feature_map_size;

        std::cout.width(3); std::cout<<std::right<<i;
        std::cout<<" ";
        std::cout.width(16); std::cout<<std::left<<layers[i]->getLayerName();
        std::cout.width(4);  std::cout<<std::right<<in.h;
        std::cout<<" x ";
        std::cout.width(4);  std::cout<<std::right<<in.w;
        std::cout<<", ";
        std::cout.width(4);  std::cout<<std::right<<in.c;
        std::cout<<"  -> ";
        std::cout.width(4);  std::cout<<std::right<<out.h;
        std::cout<<" x ";
        std::cout.width(4);  std::cout<<std::right<<out.w;
        std::cout<<", ";
        std::cout.width(4);  std::cout<<std::right<<out.c;
        std::cout<<"\n";
    }
    printCenteredTitle("", '=', 60);
    std::cout<<"\n";
    std::cout<<"N params: "<<tot_params<<std::endl;
    std::cout<<"Max feature map size: "<<max_feature_map_size<<std::endl;
    std::cout<<"N MACC: "<<tot_MACC<<std::endl<<std::endl;
    printCudaMemUsage();
}
const char *Network::getNetworkRTName(const char *network_name){
    networkName = network_name;
    int network_name_len = strlen(network_name);
    char *RTName = (char *)malloc((network_name_len + 9)*sizeof(char));
    if (fp16){
        strcpy(RTName, network_name);
        strcat(RTName, "_fp16.rt");
        RTName[network_name_len + 8] = '\0';
    }
    else if (dla){
        strcpy(RTName, network_name);
        strcat(RTName, "_dla.rt");
        RTName[network_name_len + 7] = '\0';
    }
        
    else if (int8){
        strcpy(RTName, network_name);
        strcat(RTName, "_int8.rt");
        RTName[network_name_len + 8] = '\0';
    }
        
    else{
        strcpy(RTName, network_name);
        strcat(RTName, "_fp32.rt");
        RTName[network_name_len + 8] = '\0';
    }
    networkNameRT = RTName;
    return RTName;
}


}}
