/*
 *  V4L2 video capture: My tests
 *
 *  Nome: Josemar Simão
 *  email: josemars@ifes.edu.br
 *
 *  This program can be used and distributed without restrictions.
 *
 *  This program is provided with the V4L2 API
 *  see http://linuxtv.org/docs.php for more information
 *
 *  ---------------------------------------------------------------------------
 *  camascii.h - IMAGEM (cv::Mat) COMO TEXTO PARA O TERMINAL
 *
 *  Etapa 5: antes cam_asciiart_X() ficava em multiview.cpp e lia a variável global
 *  'title' do programa. Agora o título é um parâmetro, e a função é da biblioteca.
 *  ---------------------------------------------------------------------------
 */

#ifndef CAMASCII_H_INCLUDED
#define CAMASCII_H_INCLUDED

#include <opencv2/opencv.hpp>

/*
 * Monta em d_dest uma "tela" de texto (mTH linhas x mTW colunas, sem '\n') com o título
 * centralizado na 1ª linha, uma linha em branco e a imagem 'frame' reduzida e centralizada.
 * Aceita CV_8UC1 (cinza) e CV_8UC3 (usa o canal G como brilho). Não imprime nada: quem chama
 * escreve d_dest no terminal. d_dest precisa ter pelo menos mTH * mTW + 1 bytes.
 */
void cam_asciiart_X(char* d_dest, int mTH, int mTW, const cv::Mat &frame, const char *title);

/*
 * Monta em d_dest a imagem 'frame' em texto DENTRO de um retângulo de 'rows' linhas por 'cols'
 * colunas, sem título e sem '\n': são rows * cols caracteres, linha após linha (a linha i começa em
 * d_dest + i * cols). A imagem é reduzida mantendo a proporção, centralizada no retângulo, e o
 * restante é preenchido com espaços - por isso desenhar o retângulo inteiro também apaga o que
 * houvesse atrás. Quem chama imprime cada linha na posição desejada do terminal.
 * halign / valign (0..1) dizem onde a imagem fica dentro do retângulo quando sobra espaço:
 * horizontal 0 = esquerda, 0.5 = centro, 1 = direita; vertical 0 = topo, 0.5 = centro, 1 = embaixo.
 * Aceita CV_8UC1 e CV_8UC3. d_dest precisa ter pelo menos rows * cols + 1 bytes.
 * used_rows / used_cols (podem ser NULL) recebem o tamanho que a imagem realmente ocupou.
 * Retângulo vazio ou imagem inválida: preenche tudo com espaços.
 */
void cam_asciiart_rect(char *d_dest, int rows, int cols, const cv::Mat &frame, float halign, float valign, int *used_rows, int *used_cols);

#endif /* CAMASCII_H_INCLUDED */
