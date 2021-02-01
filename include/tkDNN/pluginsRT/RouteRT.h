#include<cassert>
#include "../kernels.h"

#include <cublas_v2.h>

class RouteRT : public IPluginExt {

	/**
		THIS IS NOT USED ANYMORE
	*/

public:
	RouteRT(int groups, int group_id) {
		this->groups = groups;
		this->group_id = group_id;
	}

	~RouteRT(){

	}

	int getNbOutputs() const override {
		return 1;
	}

	Dims getOutputDimensions(int index, const Dims* inputs, int nbInputDims) override {
		int out_c = 0;
		for(int i=0; i<nbInputDims; i++) out_c += inputs[i].d[0];
		return DimsCHW{out_c/groups, inputs[0].d[1], inputs[0].d[2]};
	}

	/*void configure(const Dims* inputDims, int nbInputs, const Dims* outputDims, int nbOutputs, int maxBatchSize) override {
		in = nbInputs;
		c = 0;
		for(int i=0; i<nbInputs; i++) {
			c_in[i] = inputDims[i].d[0]; 
			c += inputDims[i].d[0];
		}
		h = inputDims[0].d[1];
		w = inputDims[0].d[2];
		c /= groups;
	}*/

	int initialize() override {

		return 0;
	}

	virtual void terminate() override {
	}

	virtual size_t getWorkspaceSize(int maxBatchSize) const override {
		return 0;
	}

	virtual int enqueue(int batchSize, const void*const * inputs, void** outputs, void* workspace, cudaStream_t stream) override {
	
		if(mDataType == nvinfer1::DataType::kFLOAT)
		{
			dnnType *dstData = reinterpret_cast<dnnType*>(outputs[0]);

			for(int b=0; b<batchSize; b++) {
				int offset = 0;
				for(int i=0; i<in; i++) {
					dnnType *input = (dnnType*)reinterpret_cast<const dnnType*>(inputs[i]);
					int in_dim = c_in[i]*h*w;
					int part_in_dim = in_dim / this->groups;
					checkCuda( cudaMemcpyAsync(dstData + b*c*w*h + offset, input + b*c*w*h*groups + this->group_id*part_in_dim, part_in_dim*sizeof(dnnType), cudaMemcpyDeviceToDevice, stream) );
					offset += part_in_dim;
				}
			}
		}
		else
		{
			__half *dstData = reinterpret_cast<__half*>(outputs[0]);

			for(int b=0; b<batchSize; b++) {
				int offset = 0;
				for(int i=0; i<in; i++) {
					__half *input = (__half*)reinterpret_cast<const __half*>(inputs[i]);
					int in_dim = c_in[i]*h*w;
					int part_in_dim = in_dim / this->groups;
					checkCuda( cudaMemcpyAsync(dstData + b*c*w*h + offset, input + b*c*w*h*groups + this->group_id*part_in_dim, part_in_dim*sizeof(__half), cudaMemcpyDeviceToDevice, stream) );
					offset += part_in_dim;
				}
			}

		}

		return 0;
	}


	virtual size_t getSerializationSize() override {
		return (6+MAX_INPUTS)*sizeof(int) + sizeof(mDataType);
	}

	virtual void serialize(void* buffer) override {
		char *buf = reinterpret_cast<char*>(buffer);
		tk::dnn::writeBUF(buf, groups);
		tk::dnn::writeBUF(buf, group_id);
		tk::dnn::writeBUF(buf, in);
		for(int i=0; i<MAX_INPUTS; i++)
			tk::dnn::writeBUF(buf, c_in[i]);

		tk::dnn::writeBUF(buf, c);
		tk::dnn::writeBUF(buf, h);
		tk::dnn::writeBUF(buf, w);
		tk::dnn::writeBUF(buf, mDataType);
	}

	bool supportsFormat(nvinfer1::DataType type, nvinfer1::PluginFormat format) const override {
		return ((type == nvinfer1::DataType::kFLOAT || type == nvinfer1::DataType::kHALF) && format == nvinfer1::PluginFormat::kNCHW);
	}

	void configureWithFormat(const Dims* inputDims, int32_t nbInputs, const Dims* outputDims, int32_t nbOutputs,
							         nvinfer1::DataType type, nvinfer1::PluginFormat format, int32_t maxBatchSize) override {
		in = nbInputs;
		c = 0;
		for(int i=0; i<nbInputs; i++) {
			c_in[i] = inputDims[i].d[0]; 
			c += inputDims[i].d[0];
		}
		h = inputDims[0].d[1];
		w = inputDims[0].d[2];
		c /= groups;
		mDataType = type;
	}

	static const int MAX_INPUTS = 4;
	int in;
	int c_in[MAX_INPUTS];
	int c, h, w;
	int groups, group_id;
	nvinfer1::DataType mDataType{nvinfer1::DataType::kFLOAT};
};
