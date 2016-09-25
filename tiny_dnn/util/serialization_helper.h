/*
    Copyright (c) 2016, Taiga Nomi
    All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the tiny-dnn nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY 
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include <typeindex>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include "tiny_dnn/util/nn_error.h"
#include "tiny_dnn/util/macro.h"

namespace tiny_dnn {

class layer;

template <typename InputArchive, typename OutputArchive>
class serialization_helper {
public:
    void register_loader(const std::string& name, std::function<std::shared_ptr<layer>(InputArchive&)> func) {
        loaders_[name] = [=](void* ar) {
            return func(*reinterpret_cast<InputArchive*>(ar));
        };
    }

    template <typename T>
    void register_saver(const std::string& name, std::function<void(OutputArchive&, const T*)> func) {
        savers_[name] = [=](void* ar, const layer* l) {
            return func(*reinterpret_cast<OutputArchive*>(ar), dynamic_cast<const T*>(l));
        };
    }

    template <typename T>
    void register_type(const std::string& name) {
        type_names_[typeid(T)] = name;
    }

    std::shared_ptr<layer> load(const std::string& layer_name, InputArchive& ar) {
        if (loaders_.find(layer_name) == loaders_.end()) {
            throw nn_error("Failed to generate layer. Generator for " + layer_name + " is not found.\n"
                           "Please use CNN_REGISTER_LAYER_DESERIALIZER macro to register appropriate generator");
        }

        return loaders_[layer_name](reinterpret_cast<void*>(&ar));
    }

    void save(const std::string& layer_name, OutputArchive & ar, const layer *l) {
        if (savers_.find(layer_name) == savers_.end()) {
            throw nn_error("Failed to generate layer. Generator for " + layer_name + " is not found.\n"
                "Please use CNN_REGISTER_LAYER_DESERIALIZER macro to register appropriate generator");
        }

        savers_[layer_name](reinterpret_cast<void*>(&ar), l);
    }

    std::string serialization_name(std::type_index index) const {
        if (type_names_.find(index) == type_names_.end()) {
            throw nn_error("Typename is not registered");
        }
        return type_names_.at(index);
    }

    static serialization_helper& get_instance() {
        static serialization_helper instance;
        return instance;
    }
private:
    /** layer-type -> generator  */
    std::map<std::string, std::function<std::shared_ptr<layer>(void*)>> loaders_;

    std::map<std::string, std::function<void(void*, const layer*)>> savers_;

    std::map<std::type_index, std::string> type_names_;

    serialization_helper() {}
};

namespace detail {

template <typename InputArchive, typename T>
std::shared_ptr<T> load_layer_impl(InputArchive& ia) {

    using ST = typename std::aligned_storage<sizeof(T), CNN_ALIGNOF(T)>::type;

    auto valid = std::make_shared<bool>(false);

    std::shared_ptr<T> bn;
    bn.reset(reinterpret_cast<T*>(new ST()),
        [=](T* t) {
        if (*valid)
            t->~T();
        delete reinterpret_cast<ST*>(t);
    });

    cereal::memory_detail::LoadAndConstructLoadWrapper<InputArchive, T> wrapper(bn.get());

    wrapper.CEREAL_SERIALIZE_FUNCTION_NAME(ia);

    *valid = true;
    return bn;
}

template <typename OutputArchive, typename T>
void save_layer_impl(OutputArchive& oa, const T *layer) {
    typedef typename cereal::traits::detail::get_input_from_output<OutputArchive>::type InputArchive;

    oa (cereal::make_nvp(serialization_helper<InputArchive, OutputArchive>::get_instance().serialization_name(typeid(T)), *layer));
}

template <typename InputArchive, typename OutputArchive, typename T>
struct automatic_layer_generator_register {
    explicit automatic_layer_generator_register(const std::string& s) {
        serialization_helper<InputArchive, OutputArchive>::get_instance().register_loader(s, load_layer_impl<InputArchive, T>);
        serialization_helper<InputArchive, OutputArchive>::get_instance().template register_type<T>(s);
        serialization_helper<InputArchive, OutputArchive>::get_instance().template register_saver<T>(s, save_layer_impl<OutputArchive, T>);
    }
};

} // namespace detail

template <typename OutputArchive, typename T>
void serialize_prolog(OutputArchive& oa, const T*) {
    typedef typename cereal::traits::detail::get_input_from_output<OutputArchive>::type InputArchive;

    oa(cereal::make_nvp("type",
                        serialization_helper<InputArchive, OutputArchive>::get_instance()
                        .serialization_name(typeid(T))));
}

template <typename T>
void start_loading_layer(T & ar) {}

template <typename T>
void finish_loading_layer(T & ar) {}

inline void start_loading_layer(cereal::JSONInputArchive & ia) { ia.startNode(); }

inline void finish_loading_layer(cereal::JSONInputArchive & ia) { ia.finishNode(); }

} // namespace tiny_dnn

#define CNN_REGISTER_LAYER_SERIALIZER_BODY(layer_type, layer_name, unique_name) \
static tiny_dnn::detail::automatic_layer_generator_register<cereal::JSONInputArchive, cereal::JSONOutputArchive, layer_type> s_register_##unique_name(layer_name);\
static tiny_dnn::detail::automatic_layer_generator_register<cereal::BinaryInputArchive, cereal::BinaryOutputArchive, layer_type> s_register_binary_##unique_name(layer_name)

#define CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, activation_type, layer_name) \
CNN_REGISTER_LAYER_SERIALIZER_BODY(layer_type<tiny_dnn::activation::activation_type>, #layer_name "<" #activation_type ">", layer_name##_##activation_type)

/**
 * Register layer serializer
 * Once you define, you can create layer from text via generte_layer(InputArchive)
 **/
#define CNN_REGISTER_LAYER_SERIALIZER(layer_type, layer_name) \
CNN_REGISTER_LAYER_SERIALIZER_BODY(layer_type, #layer_name, layer_name)

/**
 * Register layer serializer
 * @todo we need to find better (easier to maintain) way to handle multiple activations
 **/
#define CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATIONS(layer_type, layer_name) \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, tan_h, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, softmax, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, identity, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, sigmoid, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, relu, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, leaky_relu, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, elu, layer_name); \
CNN_REGISTER_LAYER_SERIALIZER_WITH_ACTIVATION(layer_type, tan_hp1m2, layer_name)


