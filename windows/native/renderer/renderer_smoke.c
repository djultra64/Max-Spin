/* Native Windows SDK/driver smoke only, not a Top Spin game test. djultra64. */
#include "d3d8_combiners.h"
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern UINT d3d8_format_bpp(D3DFORMAT fmt);
int main(void)
{
    int status=1;
    NV2ACombinerState state={0};
    char text[65536];
    shaderc_compiler_t compiler=NULL;
    shaderc_compile_options_t options=NULL;
    shaderc_compilation_result_t result=NULL;
    VkInstance instance=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkShaderModule module=VK_NULL_HANDLE;
    VkPhysicalDevice *physical=NULL;
    VkQueueFamilyProperties *families=NULL;
    uint32_t count=0,queue_count=0,family=UINT32_MAX;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="Top Spin native Windows renderer smoke",
        .pEngineName="djultra64 / xboxrecomp",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    if(sizeof(DWORD)!=4 || sizeof(void*)!=8 || d3d8_format_bpp(D3DFMT_A8R8G8B8)!=32 ||
       d3d8_format_bpp(D3DFMT_R5G6B5)!=16) goto done;
    int bytes=d3d8_combiners_generate_hlsl(&state,text,sizeof text);
    if(bytes<=0 || bytes>=(int)sizeof text) goto done;
    compiler=shaderc_compiler_initialize();
    options=shaderc_compile_options_initialize();
    if(!compiler || !options) goto done;
    shaderc_compile_options_set_source_language(options,shaderc_source_language_hlsl);
    shaderc_compile_options_set_hlsl_io_mapping(options,true);
    result=shaderc_compile_into_spv(compiler,text,(size_t)bytes,shaderc_fragment_shader,
        "actual_combiner.hlsl","main",options);
    if(!result || shaderc_result_get_compilation_status(result)!=shaderc_compilation_status_success){
        if(result)fprintf(stderr,"shaderc: %s\n",shaderc_result_get_error_message(result));
        goto done;
    }
    if(vkCreateInstance(&ici,NULL,&instance)!=VK_SUCCESS ||
       vkEnumeratePhysicalDevices(instance,&count,NULL)!=VK_SUCCESS || !count || count>64)goto done;
    physical=calloc(count,sizeof *physical);
    if(!physical || vkEnumeratePhysicalDevices(instance,&count,physical)!=VK_SUCCESS)goto done;
    VkPhysicalDevice selected=physical[0];
    for(uint32_t i=0;i<count;i++){
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(physical[i],&p);
        if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){selected=physical[i];break;}
    }
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(selected,&properties);
    vkGetPhysicalDeviceQueueFamilyProperties(selected,&queue_count,NULL);
    if(!queue_count || queue_count>256)goto done;
    families=calloc(queue_count,sizeof *families);
    if(!families)goto done;
    vkGetPhysicalDeviceQueueFamilyProperties(selected,&queue_count,families);
    for(uint32_t i=0;i<queue_count;i++)if(families[i].queueCount && (families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family=i;break;}
    if(family==UINT32_MAX)goto done;
    float priority=1.f;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
    if(vkCreateDevice(selected,&dci,NULL,&device)!=VK_SUCCESS)goto done;
    VkShaderModuleCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=shaderc_result_get_length(result),.pCode=(const uint32_t*)shaderc_result_get_bytes(result)};
    if(vkCreateShaderModule(device,&sci,NULL,&module)!=VK_SUCCESS)goto done;
    printf("PASS native Windows x64: actual formats + combiner HLSL %d bytes -> SPIR-V %zu bytes; Vulkan shader module on %s\n",
        bytes,sci.codeSize,properties.deviceName);
    puts("No title, guest simulation, window, draw, save or performance acceptance in this smoke.");
    status=0;
done:
    if(status)fputs("FAIL native renderer smoke\n",stderr);
    if(module)vkDestroyShaderModule(device,module,NULL);
    if(device)vkDestroyDevice(device,NULL);
    if(instance)vkDestroyInstance(instance,NULL);
    free(families);free(physical);
    if(result)shaderc_result_release(result);
    if(options)shaderc_compile_options_release(options);
    if(compiler)shaderc_compiler_release(compiler);
    return status;
}
