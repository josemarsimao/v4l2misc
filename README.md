# v4l2misc

Biblioteca de captura de vídeo para Linux sobre a API V4L2 (Video for Linux 2). Serve três tipos de programa:

- **diagnóstico:** só consulta o dispositivo, sem capturar (ex.: [diagcam](https://github.com/josemarsimao/diagcam));
- **câmera única:** uma câmera e uma thread, com saída em modo texto (ex.: [icap](https://github.com/josemarsimao/icap));
- **multicâmera:** uma thread de captura por câmera, com OpenCV (ex.: [multiview](https://github.com/josemarsimao/multiview)).

## Módulos

```
icap      -> v4l2misc (câmera única) ----\
multiview -> camview  (multicâmera) -----+-> v4l2core (motor de captura)
diagcam   -> v4l2enum + v4l2core
```

| Módulo | Linguagem | Responsabilidade |
|---|---|---|
| `v4l2core.c/h` | C | motor de captura: abrir, configurar, alocar buffers, ligar o streaming, ler frames e liberar. Não chama `exit()`: todo erro volta como código negativo. |
| `v4l2enum.c/h` | C | descoberta de dispositivos em `/dev`, listagem de capacidades, entradas/saídas, controles, formatos, tamanhos e intervalos, e menus de escolha |
| `v4l2ctl.c/h` | C | controles da câmera (brilho, contraste, exposição...) em percentual da faixa |
| `v4l2gray.c/h` | C | conversão de pixels para tons de cinza (8 bits); é o único módulo que usa a libjpeg |
| `v4l2term.c/h` | C | terminal, cursor e arte ASCII |
| `v4l2misc.c/h` | C | fachada de câmera única. Inclui os módulos acima, então quem já usava `v4l2misc.h` continua com a mesma API |
| `camview.cpp/h` | C++ | multicâmera: `struct viod` (uma câmera e sua thread), `cam_thread_v4l2`, lista de câmeras, controle de exibição e de processos |
| `camconv.cpp/h` | C++ | conversão do frame bruto (YUYV, MJPEG, NV12, Bayer...) para `cv::Mat` BGR |
| `camascii.cpp/h` | C++ | `cv::Mat` como texto para o terminal |

Use só os módulos de que o programa precisa. O diagcam, por exemplo, compila apenas `v4l2enum` e `v4l2core`, sem libjpeg e sem OpenCV.

## Dependências

- Linux com V4L2.
- `libjpeg`: só para `v4l2gray` (e, portanto, `v4l2misc`).
- OpenCV 4 e `pthread`: só para `camview`, `camconv` e `camascii`.
- [kbhit](https://github.com/josemarsimao/kbhit) para a leitura de teclado nos menus.

## Uso

Não há um alvo de compilação próprio. Cada programa acrescenta ao seu projeto os arquivos `.c`/`.cpp` dos módulos que usa e aponta `-I../v4l2misc -I../kbhit`. Por isso, os repositórios ficam lado a lado na mesma pasta-mãe:

```
projetos/
├── v4l2misc/
├── kbhit/
├── multiview/
├── icap/
└── diagcam/
```

## Autor

Josemar Simão (josemars@ifes.edu.br)
