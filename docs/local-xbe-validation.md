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

## 4. Diagnóstico no Android ARM64 (Retroid Pocket 6)

O GitHub Actions também compila
`hulk-xbe-local-diagnostic-android-arm64`, um **executável CLI** para
Android (não um APK). A validação de memória de 4 GiB no QEMU/Linux
**não garante** que a reserva funcione no Android do RP6.

Um teste opcional por ADB com o próprio XBE:

```bash
adb push hulk-xbe-local-diagnostic-android-arm64 /data/local/tmp/hulk-xbe-check
adb push /caminho/local/default.xbe /data/local/tmp/hulk-own.xbe
adb shell chmod 755 /data/local/tmp/hulk-xbe-check
adb shell /data/local/tmp/hulk-xbe-check /data/local/tmp/hulk-own.xbe
adb shell rm /data/local/tmp/hulk-own.xbe /data/local/tmp/hulk-xbe-check
```

O comando não renderiza nem executa o Hulk. Ele verifica a imagem e
sua alocação de memória no Android real. O XBE pessoal é copiado
temporariamente para o dispositivo pelo próprio usuário, **nunca**
para o repositório. Não envie os arquivos de jogo junto dos logs.

Em alguns dispositivos a reserva de 4 GiB pode falhar por restrições
de endereçamento, memória virtual ou ambiente de execução. Nesse
caso, o diagnóstico deve retornar uma mensagem de erro, não afirmar
que o motor funciona.

## 5. Recompilação real em ambiente local (EU/USA)

O projeto agora possui `tools/recompile_hulk_local.py`, que executa
o **recompilador oficial** sobre o XBE original, selecionando
automaticamente `config/game.toml` (Europa) ou
`config/game_usa.toml` (EUA) usando o título e o ponto de entrada.
Ele valida a identificação antes da recompilação e usa o XDK 5849
do SDK oficial.

Este processo é privado: o conteúdo de `--generated` contém código
derivado da sua cópia do jogo e **não deve ser enviado ao GitHub**.

```bash
python3 tools/recompile_hulk_local.py \
  --xbe /caminho/privado/default.xbe \
  --sdk /caminho/XboxXBERecomp \
  --upstream /caminho/hulk-ultimate-destruction-recomp-xbox-ver- \
  --recompiler /caminho/xbe_recomp_host \
  --generated /caminho/privado/generated
```

Depois, use `tools/build_xbe_android.py` com `--generated`.
O ELF só será aceito se todos os símbolos requeridos estiverem
disponíveis. Hoje ainda faltam serviços de kernel, hooks, GPU e
áudio — **não espere que todo o Hulk compile com sucesso**.

Os testes automatizados usam um XBE fictício contendo apenas
`mov eax, 0x12345678; ret`, sem código comercial. Assim é
possível verificar a ferramenta de recompilação inteira e a
geração ARM64 em GitHub Actions, sem publicar arquivos de jogo.

## 6. Teste original de instruções x86 recompiladas (sem Hulk)

`tools/create_synthetic_xbe_recompile.py` cria um XBE **original e
sintético**, contendo somente as instruções x86:
`mov eax, 0x12345678; ret`. Não há dados comerciais do Hulk.

O workflow `xbe-recomp-arm64.yml` usa o **recompilador real**
`xbe_recomp_host` para traduzir esses bytes, passa o C++ gerado
pela preparação ELF e compila o resultado como módulo ARM64.
Também contém um teste Linux que carrega as seções do XBE,
despacha a função recompilada e confirma o valor retornado em
`X86Context.eax`. O teste nativo ARM64/QEMU e o executável
Android ARM64 foram adicionados, mas precisam passar no CI.

Quando publicado, o artefato
`hulkpocket-genuine-x86-opcodes-arm64-smoke` deverá conter um
**executável Android e o XBE de teste sintético**, para execução
opcional por ADB no RP6, sem precisar do jogo comercial:

```bash
adb push hulk-synthetic-xbe-opcodes-android-arm64 /data/local/tmp/hulk-x86-smoke
adb push synthetic.xbe /data/local/tmp/hulk-test.xbe
adb shell chmod 755 /data/local/tmp/hulk-x86-smoke
adb shell /data/local/tmp/hulk-x86-smoke /data/local/tmp/hulk-test.xbe
adb shell rm /data/local/tmp/hulk-x86-smoke /data/local/tmp/hulk-test.xbe
```

O teste deve confirmar que **código originalmente x86, recompilado
pela ferramenta oficial e compilado em ARM64**, é executado com
a memória virtual Xbox no dispositivo. Ele não renderiza gráficos,
não usa o Hulk e não demonstra que os serviços Xbox necessários
ao jogo estão implementados.

## 7. Mapear imports e recursos de inicialização do XBE local (Android)

O diagnóstico CLI ganhou `--imports`, para ler **somente a
tabela de imports do kernel Xbox no XBE original**. Ele informa
a quantidade de entries, a quantidade de ordinais únicos e
a lista dos ordinais solicitados. Não chama nenhum deles.
A leitura é limitada, valida o fim da tabela e rejeita entradas
que não sejam descritores originais `0x80000000 | ordinal`.
O arquivo deve ser uma cópia obtida legitimamente.

O relatório também imprime `tls`, `stack_commit`,
`heap_reserve`, `heap_commit` e `init_flags`.
Esses campos são apenas inventariados: **não há
inicialização de heap, pilha, TLS ou kernel Xbox ainda**.

No Termux, a partir de uma cópia local do XBE e do executável
Android AArch64 atualizado do artefato
`hulkpocket-xbe-preflight-cli-arm64`:

```bash
chmod +x ~/hulk-xbe-local-diagnostic-android-arm64
~/hulk-xbe-local-diagnostic-android-arm64 ~/default.xbe --imports
```

O resultado de ordinais é útil para planejar serviços e
registrar quais dependências precisam ser escritas. Evite
enviar seu XBE, ISO, bibliotecas geradas ou arquivos de
jogo completos; compartilhe apenas o texto do relatório
se quiser analisar o que falta. Não há suporte a
descompactar imagens de disco dentro desse diagnóstico.

**Limite importante:** o inventário é estático.
Um ordinal listado não prova que ele será chamado na
inicialização nem indica a ordem das chamadas. Um símbolo
não listado ainda pode ser acessado por outros mecanismos,
como hooks do XDK ou imports dinâmicos.
