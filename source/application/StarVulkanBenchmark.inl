// Included in SdlVulkanDiagnostic's public section. Test-NRO path only.
  void runOffscreenBenchmark() {
    using Clock=std::chrono::steady_clock;
    constexpr uint64_t Timeout=2'000'000'000ULL;
    if(!m_worldPipeline||!m_worldFramebuffer||!m_renderer)throw RendererException("GPU benchmark requires the verified world scene");
    Logger::info("[gpu] offscreen-fill-v1: world shader {}x{}, 1/4/16 full-screen alpha draws; 2 warm-ups + 8 measured submissions each; no timed presentation",m_extent.width,m_extent.height);
    checkVk(vkWaitForFences(m_device,1,&m_fence,VK_TRUE,Timeout),"benchmark initial fence");
    m_renderer->startFrame();m_renderer->setScreenSize({m_extent.width,m_extent.height});
    m_renderer->switchEffectConfig("world");m_renderer->setScissorRect({});
    m_renderer->setEffectParameter("lightMapEnabled",false);
    m_renderer->setEffectTexture("lightMap",Image());m_renderer->setEffectTexture("tileLightMap",Image());
    m_renderer->render(RenderQuad(RectF::withSize(Vec2F(),Vec2F(float(m_extent.width),float(m_extent.height))),{80,130,190,128},0.0f));
    m_renderer->flush();uploadGeometry();
    // prepareTextures records transfers and builds descriptors, so complete a
    // separate setup submission before timing any draw workload.
    checkVk(vkResetCommandBuffer(m_commands,0),"benchmark setup reset");
    VkCommandBufferBeginInfo setupBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    setupBegin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(m_commands,&setupBegin),"benchmark setup begin");
    prepareTextures();
    checkVk(vkEndCommandBuffer(m_commands),"benchmark setup end");
    checkVk(vkResetFences(m_device,1,&m_fence),"benchmark setup fence reset");
    VkSubmitInfo setupSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};setupSubmit.commandBufferCount=1;setupSubmit.pCommandBuffers=&m_commands;
    checkVk(vkQueueSubmit(m_queue,1,&setupSubmit,m_fence),"benchmark setup submit");
    checkVk(vkWaitForFences(m_device,1,&m_fence,VK_TRUE,Timeout),"benchmark setup completion");
    if(m_drawDescriptors.empty())throw RendererException("GPU benchmark descriptor missing");
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(m_physical,&properties);
    uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(m_physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(m_physical,&count,families.data());
    auto bits=m_queueFamily<families.size()?families[m_queueFamily].timestampValidBits:0;
    auto createQueries=reinterpret_cast<PFN_vkCreateQueryPool>(vkGetDeviceProcAddr(m_device,"vkCreateQueryPool"));
    auto resetQueries=reinterpret_cast<PFN_vkCmdResetQueryPool>(vkGetDeviceProcAddr(m_device,"vkCmdResetQueryPool"));
    auto writeTimestamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(vkGetDeviceProcAddr(m_device,"vkCmdWriteTimestamp"));
    auto readQueries=reinterpret_cast<PFN_vkGetQueryPoolResults>(vkGetDeviceProcAddr(m_device,"vkGetQueryPoolResults"));
    m_destroyBenchmarkQueries=reinterpret_cast<PFN_vkDestroyQueryPool>(vkGetDeviceProcAddr(m_device,"vkDestroyQueryPool"));
    bool timestamps=bits>0&&bits<=64&&std::isfinite(properties.limits.timestampPeriod)&&properties.limits.timestampPeriod>0
        &&createQueries&&resetQueries&&writeTimestamp&&readQueries&&m_destroyBenchmarkQueries;
    if(timestamps) {
      VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
      auto result=createQueries(m_device,&query,nullptr,&m_benchmarkQueries);
      if(result!=VK_SUCCESS){Logger::info("[gpu] timestamps unavailable: query pool result={}",int(result));timestamps=false;}
    }
    Logger::info("[gpu] device={} driver=0x{:x} timestamps={} timestamp-bits={} timestamp-period-ns={:.6f}",properties.deviceName,properties.driverVersion,timestamps,bits,properties.limits.timestampPeriod);
    if(!timestamps)Logger::info("[gpu] GPU-only timing unavailable; submit-to-fence wall time includes CPU/driver/scheduling");
    for(unsigned draws:{1u,4u,16u}) {
#ifdef __SWITCH2__
      if(!appletMainLoop())break;
#endif
      checkVk(vkResetCommandBuffer(m_commands,0),"benchmark reset commands");
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};checkVk(vkBeginCommandBuffer(m_commands,&begin),"benchmark begin commands");
      if(timestamps){resetQueries(m_commands,m_benchmarkQueries,0,2);writeTimestamp(m_commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_benchmarkQueries,0);}
      VkClearValue clear{};clear.color.float32[3]=1;
      VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      render.renderPass=m_worldClearPass;render.framebuffer=m_worldFramebuffer;render.renderArea.extent=m_extent;
      render.clearValueCount=1;render.pClearValues=&clear;vkCmdBeginRenderPass(m_commands,&render,VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(m_commands,VK_PIPELINE_BIND_POINT_GRAPHICS,m_worldPipeline);
      VkViewport viewport{0,float(m_extent.height),float(m_extent.width),-float(m_extent.height),0,1};VkRect2D scissor{{0,0},m_extent};
      vkCmdSetViewport(m_commands,0,1,&viewport);vkCmdSetScissor(m_commands,0,1,&scissor);
      VkDeviceSize offset=0;vkCmdBindVertexBuffers(m_commands,0,1,&m_vertices.buffer,&offset);
      vkCmdBindDescriptorSets(m_commands,VK_PIPELINE_BIND_POINT_GRAPHICS,m_layout,0,1,&m_drawDescriptors.front(),0,nullptr);
      PushState push{};for(unsigned i=0;i<3;++i)push.rows[i][i]=1;
      push.screen[0]=float(m_extent.width);push.screen[1]=float(m_extent.height);
      vkCmdPushConstants(m_commands,m_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
      for(unsigned i=0;i<draws;++i)vkCmdDraw(m_commands,6,1,0,0);
      vkCmdEndRenderPass(m_commands);
      if(timestamps)writeTimestamp(m_commands,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_benchmarkQueries,1);
      checkVk(vkEndCommandBuffer(m_commands),"benchmark end commands");
      std::vector<double> wallTimes,gpuTimes;
      for(unsigned sample=0;sample<10;++sample) {
#ifdef __SWITCH2__
        if(!appletMainLoop())break;
#endif
        checkVk(vkResetFences(m_device,1,&m_fence),"benchmark reset fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&m_commands;
        auto started=Clock::now();checkVk(vkQueueSubmit(m_queue,1,&submit,m_fence),"benchmark submit");
        checkVk(vkWaitForFences(m_device,1,&m_fence,VK_TRUE,Timeout),"benchmark completion fence");
        double wall=std::chrono::duration<double,std::milli>(Clock::now()-started).count();if(sample>=2)wallTimes.push_back(wall);
        if(timestamps) {
          uint64_t values[2]{};auto result=readQueries(m_device,m_benchmarkQueries,0,2,sizeof(values),values,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
          uint64_t mask=bits==64?UINT64_MAX:((uint64_t(1)<<bits)-1);
          double ms=((values[1]-values[0])&mask)*double(properties.limits.timestampPeriod)/1e6;
          if(result==VK_SUCCESS&&ms>0&&std::isfinite(ms)&&ms<2000){if(sample>=2)gpuTimes.push_back(ms);}
          else Logger::info("[gpu] timestamp sample unavailable/invalid result={} duration-ms={:.6f}",int(result),ms);
        }
      }
      if(!wallTimes.empty())Logger::info("[gpu-summary] draws={} pixels-per-draw={} samples={} submit-fence-median-ms={:.4f} min={:.4f} max={:.4f}",draws,uint64_t(m_extent.width)*m_extent.height,wallTimes.size(),PerformanceKernels::median(wallTimes),*std::min_element(wallTimes.begin(),wallTimes.end()),*std::max_element(wallTimes.begin(),wallTimes.end()));
      if(!gpuTimes.empty())Logger::info("[gpu-timestamp-summary] draws={} samples={} median-ms={:.4f} min={:.4f} max={:.4f}",draws,gpuTimes.size(),PerformanceKernels::median(gpuTimes),*std::min_element(gpuTimes.begin(),gpuTimes.end()),*std::max_element(gpuTimes.begin(),gpuTimes.end()));
    }
    if(m_benchmarkQueries){m_destroyBenchmarkQueries(m_device,m_benchmarkQueries,nullptr);m_benchmarkQueries=VK_NULL_HANDLE;}
    Logger::info("[gpu] complete; pipeline creation, uploads, readback, presentation and VSync excluded from timed submissions");
  }
