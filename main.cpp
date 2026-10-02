#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>

// ==========================================
// STRUCTURE DES VERTICES (Style RPG / Avancé)
// Représente un sommet avec position, couleur, normale et texture
// ==========================================
struct Vertex {
    float pos[3];     // X, Y, Z (Positions)
    float color[3];   // R, G, B (Couleurs de base)
    float normal[3];  // NX, NY, NZ (Normales pour l'éclairage)
    float texCoord[2];// U, V (Coordonnées de texture)
};

// Variables globales Vulkan & Fenêtre
VkInstance instance = VK_NULL_HANDLE;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
std::vector<VkImage> swapchainImages;
VkFormat swapchainImageFormat;
VkExtent2D swapchainExtent;
std::vector<VkImageView> swapchainImageViews;

VkRenderPass renderPass = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;
std::vector<VkFramebuffer> swapchainFramebuffers;
VkCommandPool commandPool = VK_NULL_HANDLE;
VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

// Tampons pour les vertices (Gestion des chunks / gros volumes)
VkBuffer vertexBuffer = VK_NULL_HANDLE;
VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
VkFence inFlightFence = VK_NULL_HANDLE;

// Fonction utilitaire pour lire les fichiers SPIR-V compilés
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0); // Quitter en appuyant sur Échap
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// Fonction utilitaire pour trouver un type de mémoire compatible sur le GPU
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

bool initVulkan(HWND hwnd, HINSTANCE hInstance) {
    // 1. Initialisation de l'Instance Vulkan
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Moteur RPG Vulkan - Cube 3D";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) return false;

    // 2. Création de la surface Windows
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hwnd = hwnd;
    surfaceCreateInfo.hinstance = hInstance;
    if (vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface) != VK_SUCCESS) return false;

    // 3. Sélection du GPU physique
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];

    // 4. Création du dispositif logique (Logical Device)
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCI = {};
    deviceCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCreateInfo;
    deviceCI.enabledExtensionCount = 1;
    deviceCI.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);

    // 5. Configuration de la Swapchain en plein écran
    RECT rect;
    GetClientRect(hwnd, &rect);
    swapchainExtent = { (uint32_t)(rect.right - rect.left), (uint32_t)(rect.bottom - rect.top) };
    swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    VkSwapchainCreateInfoKHR swapchainCI = {};
    swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCI.surface = surface;
    swapchainCI.minImageCount = 2;
    swapchainCI.imageFormat = swapchainImageFormat;
    swapchainCI.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainCI.imageExtent = swapchainExtent;
    swapchainCI.imageArrayLayers = 1;
    swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCI.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCI.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain) != VK_SUCCESS) return false;

    uint32_t imageCount;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo colorImageViewCI = {};
        colorImageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorImageViewCI.image = swapchainImages[i];
        colorImageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageViewCI.format = swapchainImageFormat;
        colorImageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageViewCI.subresourceRange.levelCount = 1;
        colorImageViewCI.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &colorImageViewCI, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) return false;
    }

    // =========================================================================
    // 🌍 CHARGEMENT DES DONNÉES DE LA SCÈNE (JEU TYPE RPG)
    // Chargement initial unique des vertices du cube avec attributs multiples :
    // Positions (X,Y,Z), Couleurs, Normales (Éclairage) et Coordonnées de Texture (U,V)
    // =========================================================================
    std::vector<Vertex> cubeVertices = {
        // Face Avant (Exemple de données riches avec attributs complets)
         {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
         {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
         {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
         {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
         {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
         {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
         // (Dans un vrai RPG, on chargerait ici des millions de sommets via des chunks de terrain ou des modèles 3D complexes)
    };

    // Allocation et transfert des données dans le Vertex Buffer sur le GPU (VRAM)
    VkDeviceSize bufferSize = sizeof(cubeVertices[0]) * cubeVertices.size();
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

    // Copie effective des données de la RAM vers la VRAM du GPU
    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, cubeVertices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, vertexBufferMemory);

    // 6. Création de la Passe de Rendu (Render Pass)
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassCI = {};
    renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = 1;
    renderPassCI.pAttachments = &colorAttachment;
    renderPassCI.subpassCount = 1;
    renderPassCI.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass) != VK_SUCCESS) return false;

    // 7. Chargement des Shaders Externes (SPIR-V)
    auto vertShaderCode = readFile("vert.spv");
    auto fragShaderCode = readFile("frag.spv");
    if (vertShaderCode.empty() || fragShaderCode.empty()) return false;

    VkShaderModule vertShaderModule, fragShaderModule;
    VkShaderModuleCreateInfo vertCI = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vertShaderCode.size(), reinterpret_cast<const uint32_t*>(vertShaderCode.data())};
    VkShaderModuleCreateInfo fragCI = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, fragShaderCode.size(), reinterpret_cast<const uint32_t*>(fragShaderCode.data())};
    if (vkCreateShaderModule(device, &vertCI, nullptr, &vertShaderModule) != VK_SUCCESS) return false;
    if (vkCreateShaderModule(device, &fragCI, nullptr, &fragShaderModule) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo vertStageCI = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr};
    VkPipelineShaderStageCreateInfo fragStageCI = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr};
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageCI, fragStageCI};

    // Description des liaisons d'attributs de sommets (Binding & Attributes pour les positions, couleurs, normales, textures)
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};     // Position (X, Y, Z)
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};   // Couleur (R, G, B)
    attributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}; // Normale (Éclairage)
    attributeDescriptions[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT,   offsetof(Vertex, texCoord)};// Texture (U, V)

    VkPipelineVertexInputStateCreateInfo vertexInputCI = {};
    vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputCI.vertexBindingDescriptionCount = 1;
    vertexInputCI.pVertexBindingDescriptions = &bindingDescription;
    vertexInputCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputCI.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0.0f, 0.0f, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, swapchainExtent};
    VkPipelineViewportStateCreateInfo viewportStateCI = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &viewport, 1, &scissor};

    VkPipelineRasterizationStateCreateInfo rasterizerCI = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizerCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerCI.lineWidth = 1.0f;
    rasterizerCI.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerCI.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisamplingCI = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisamplingCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlendingCI = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendingCI.attachmentCount = 1;
    colorBlendingCI.pAttachments = &colorBlendAttachment;

    // Configuration des Push Constants pour envoyer les matrices de transformation (Rotation, Déplacement, Projection) au Shader
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 16; // Espace pour une matrice 4x4 de transformation

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout) != VK_SUCCESS) return false;

    // 8. Création du Pipeline Graphique
    VkGraphicsPipelineCreateInfo pipelineCI = {};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages;
    pipelineCI.pVertexInputState = &vertexInputCI;
    pipelineCI.pInputAssemblyState = &inputAssemblyCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizerCI;
    pipelineCI.pMultisampleState = &multisamplingCI;
    pipelineCI.pColorBlendState = &colorBlendingCI;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &graphicsPipeline) != VK_SUCCESS) return false;

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);

    // 9. Framebuffers & Command Pool
    swapchainFramebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageView attachments[] = { swapchainImageViews[i] };
        VkFramebufferCreateInfo fbCI = {};
        fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass = renderPass;
        fbCI.attachmentCount = 1;
        fbCI.pAttachments = attachments;
        fbCI.width = swapchainExtent.width;
        fbCI.height = swapchainExtent.height;
        fbCI.layers = 1;
        if (vkCreateFramebuffer(device, &fbCI, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) return false;
    }

    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = 0;
    if (vkCreateCommandPool(device, &poolCI, nullptr, &commandPool) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo allocInfoCB = {};
    allocInfoCB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfoCB.commandPool = commandPool;
    allocInfoCB.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCB.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocInfoCB, &commandBuffer) != VK_SUCCESS) return false;

    // 10. Objets de synchronisation
    VkSemaphoreCreateInfo semCI = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceCI = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    if (vkCreateSemaphore(device, &semCI, nullptr, &imageAvailableSemaphore) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(device, &semCI, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) return false;
    if (vkCreateFence(device, &fenceCI, nullptr, &inFlightFence) != VK_SUCCESS) return false;

    return true;
}

// ==========================================
// BOUCLE DE RENDU ET CALCULS DE TRANSFORMATION PAR TRAME
// ==========================================
void drawFrame() {
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    // =========================================================================
    // 🧮 TRANSFORMATIONS MATRICIELLES, ROTATION, DÉPLACEMENTS ET LUMIÈRE (CPU)
    // Effectué à chaque trame pour animer le cube du jeu RPG
    // =========================================================================
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    // Exemple de matrice de transformation 4x4 (Rotation dynamique autour des axes + Translation de déplacement)
    // Dans un projet réel, on utilise GLM (glm::mat4) pour multiplier Projection * Vue * Modèle.
    float rotationMatrix[16] = {
        cos(time), 0.0f, sin(time), 0.0f,
        0.0f,      1.0f, 0.0f,      0.0f,
       -sin(time), 0.0f, cos(time), 0.0f,
        0.0f,      0.0f, 0.0f,      1.0f  // Position / Translation (Déplacement du cube dans le monde 3D)
    };

    vkResetCommandBuffer(commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkClearValue clearColor = {{{0.05f, 0.05f, 0.1f, 1.0f}}}; // Fond couleur nuit/jeu RPG
    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass;
    rpInfo.framebuffer = swapchainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchainExtent;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Liaison du Pipeline graphique
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    // 💡 ENVOI DES TRANSFORMATIONS AU SHADER VIA LES PUSH CONSTANTS
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rotationMatrix), rotationMatrix);

    // Liaison du Vertex Buffer contenant nos données de sommets (Vertices, Normales, Couleurs, Textures)
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Ordre de dessin du cube 3D
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    // Soumission au GPU et Affichage (Presentation)
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

void cleanup() {
    vkDeviceWaitIdle(device);
    if (device) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroyFence(device, inFlightFence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        for (auto fb : swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "VulkanRPGClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // =========================================================================
    // 🖥️ CONFIGURATION PLEIN ÉCRAN (FULLSCREEN MODE)
    // Utilisation de la résolution native du moniteur pour s'afficher en plein écran
    // =========================================================================
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Moteur RPG Vulkan - Plein Écran",
        WS_POPUP | WS_VISIBLE, // Style de fenêtre sans bordure pour le plein écran
        0, 0, screenWidth, screenHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);

    if (!initVulkan(hwnd, hInstance)) {
        MessageBox(hwnd, "Erreur lors de l'initialisation de Vulkan", "Erreur Critique", MB_OK);
        cleanup();
        return 0;
    }

    // Boucle principale du jeu / application graphique
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        drawFrame();
    }

    cleanup();
    return 0;
}