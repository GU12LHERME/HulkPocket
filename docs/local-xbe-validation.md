# HulkPocket — teste local com XBE próprio

Este fluxo **não gera um APK jogável**. Ele valida se o arquivo
`default.xbe` de uma cópia própria de *Hulk Ultimate Destruction* pode ser
mapeado na memória Xbox e, se houver um ELF ARM64 gerado localmente,
confere o ponto de entrada com a tabela compilada.

Não envie seu XBE, ISO ou código recompilado para o repositório público.
Use somente bibliotecas ELF que você mesmo compilou e confia:
`dlopen` executa inicializadores antes da validação dos símbolos.

## 1. Verificação rápida do arquivo

```bash
python3 tools/xbe_info.py /caminho/local/default.xbe
```

O script informa título, ID, candidatos de região e SHA-256.
Identificar a versão pelo ponto de entrada é **preliminar**; não prova
compatibilidade de todos os endereços/hooks.

## 2. Compilar o diagnóstico de seções (Linux de 64 bits)

O diretório `$SDK` deve apontar para uma cópia da fonte oficial de
XboxXBERecomp.

```bash
SDK=/caminho/XboxXBERecomp
clang++ -std=c++20 -O2 -pthread \
  -I native -I "$SDK/runtime" \
  native/hulk_xbe_local_diagnostic.cpp native/hulk_xbe_preflight.cpp \
  native/xbe_image_memory_loader.cpp native/xbox_guest_address_space.cpp \
  native/portable_xbe_image.cpp native/portable_xbe_dispatch.cpp \
  -ldl -o /tmp/hulk_xbe_local_diagnostic

/tmp/hulk_xbe_local_diagnostic /caminho/local/default.xbe
```

Isso **não chama o código do jogo**. Somente lê e valida o cabeçalho,
mapeia as seções em um espaço virtual de 4 GiB e imprime os valores
relevantes sem expor os bytes do executável.

Se você tiver um módulo ELF **Linux do mesmo tipo de arquitetura do
diagnóstico** e de origem confiável, pode passar o caminho como segundo
argumento. Um `.so` Android ARM64 **não** pode ser aberto por um
executável Linux x86-64:

```bash
/tmp/hulk_xbe_local_diagnostic /caminho/local/default.xbe /caminho/local/modulo-linux.so
```

O diagnóstico então compara o ID do Hulk e a função de entrada com os
símbolos exportados pelo módulo. Isso também **não inicia o jogo**.

## 3. Compilação local do C++ gerado para Android

Primeiro, execute o recompilador oficial localmente com a configuração
correspondente à versão do seu XBE, gerando
`x86_func_mapping.cpp` e as partições `x86_recomp.*.cpp`.
Depois:

```bash
python3 tools/build_xbe_android.py \
  --sdk /caminho/XboxXBERecomp \
  --generated /caminho/generated \
  --ndk-bin /caminho/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin \
  --output /caminho/saida/libhulk-code.so \
  --audit-output /caminho/saida/imports.json
```

Se houver chamadas ainda não implementadas, o linker pode falhar
intencionalmente por causa de `-z defs`. O relatório
`imports.json` contém **nomes de símbolos**, classificados por
categoria, para orientar a próxima implementação. Ele não contém
seções do jogo, texturas ou o XBE. Os símbolos podem revelar nomes
internos do projeto; revise o relatório antes de compartilhá-lo.

**Ainda necessários para executar o Hulk:** exportações reais de
recompilação para ELF, memória e kernel completos, hooks de jogo,
dispositivos gráficos, áudio, controles, inicialização de threads
e validação no RP6.
