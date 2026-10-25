/*
 *  V4L2 video capture: My tests
 *
 *  Nome: Josemar Simão
 *  email: josemars@ifes.edu.br
 *
 *  This program can be used and distributed without restrictions.
 *
 *  ---------------------------------------------------------------------------
 *  v4l2ctl.c - CONTROLES DA CÂMERA EM PERCENTUAL (implementação)
 *
 *  Veja v4l2ctl.h para a descrição geral. Resumo do funcionamento:
 *    - v4l2ctl_populate() percorre os controles do driver (VIDIOC_QUERY_EXT_CTRL, com
 *      VIDIOC_QUERYCTRL como alternativa em kernels antigos) e guarda na tabela os de
 *      tipo numérico/menu/botão.
 *    - Leitura e gravação usam VIDIOC_G/S_EXT_CTRLS (aceitam 64 bits); em kernels
 *      sem essa chamada cai para VIDIOC_G/S_CTRL (32 bits).
 *    - O percentual é convertido para o valor real por v4l2ctl_pct_to_raw(), que
 *      respeita a faixa e o passo. Em menus o valor é o índice do item e é encaixado
 *      no item válido mais próximo (VIDIOC_QUERYMENU).
 *  ---------------------------------------------------------------------------
 */

#define _GNU_SOURCE                 /* strcasestr */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>

#include "v4l2ctl.h"

/* Em cabeçalhos antigos a constante não existe; 0 significa "valor atual". */
#ifndef V4L2_CTRL_WHICH_CUR_VAL
#define V4L2_CTRL_WHICH_CUR_VAL 0
#endif

#define xioctl(fh, request, arg) v4l2c_xioctl((fh), (unsigned long)(request), (arg))

/* ========================================================================== */
/*  Auxiliares internos (static)                                              */
/* ========================================================================== */

/* Arredonda para o inteiro mais próximo (metade para longe de zero), sem precisar de libm. */
static __s64 rnd_ll(long double x) {
    return (x >= 0) ? (__s64)(x + 0.5L) : -(__s64)(-x + 0.5L);
}

/* Diz se o tipo tem uma faixa que faz sentido em percentual (ou é um botão). */
static int type_supported(__u32 t) {
    return t == V4L2_CTRL_TYPE_INTEGER   || t == V4L2_CTRL_TYPE_BOOLEAN ||
           t == V4L2_CTRL_TYPE_MENU      || t == V4L2_CTRL_TYPE_INTEGER_MENU ||
           t == V4L2_CTRL_TYPE_INTEGER64 || t == V4L2_CTRL_TYPE_BUTTON;
}

/* Diz se o valor do controle é o índice de um item de menu. */
static int type_is_menu(__u32 t) {
    return t == V4L2_CTRL_TYPE_MENU || t == V4L2_CTRL_TYPE_INTEGER_MENU;
}

/* Guarda o errno da falha na tabela e converte para o código de retorno desta camada. */
static int fail_errno(v4l2ctl_set *cs, int e) {
    cs->err_no = e;
    if (e == EBUSY)                  return V4L2C_ERR_BUSY;     /* controle "preso" por outro (grabbed) */
    if (e == EACCES || e == EPERM)   return V4L2CTL_ERR_NOTSET; /* somente leitura                      */
    return V4L2C_ERR_IOCTL;
}

/*
 * Consulta UM controle pelo id exato. Tenta VIDIOC_QUERY_EXT_CTRL e, se o kernel não
 * conhece essa chamada (ENOTTY), usa VIDIOC_QUERYCTRL e converte o resultado.
 * Devolve 0 se achou, -1 (com errno) se não.
 */
static int query_one(int fid, __u32 id, struct v4l2_query_ext_ctrl *q) {
    struct v4l2_queryctrl qc;

    CLEAR(*q);
    q->id = id;
    if (0 == xioctl(fid, VIDIOC_QUERY_EXT_CTRL, q))
        return 0;
    if (errno != ENOTTY)
        return -1;

    /* Alternativa para kernels antigos: converte v4l2_queryctrl -> v4l2_query_ext_ctrl */
    CLEAR(qc);
    qc.id = id;
    if (0 != xioctl(fid, VIDIOC_QUERYCTRL, &qc))
        return -1;
    CLEAR(*q);
    q->id = qc.id;  q->type = qc.type;  q->flags = qc.flags;
    q->minimum = qc.minimum;  q->maximum = qc.maximum;
    q->step = (__u64)qc.step;  q->default_value = qc.default_value;
    memcpy(q->name, qc.name, sizeof(q->name) - 1);   /* q->name já está zerado: o terminador fica garantido */
    return 0;
}

/* Copia os campos de uma consulta do driver para um item da tabela (e corrige step = 0). */
static void fill_item(v4l2ctl_item *it, const struct v4l2_query_ext_ctrl *q) {
    it->id    = q->id;
    it->type  = q->type;
    it->flags = q->flags;
    strncpy(it->name, q->name, sizeof(it->name) - 1);
    it->name[sizeof(it->name) - 1] = '\0';          /* garante o terminador nulo */
    it->min   = q->minimum;
    it->max   = q->maximum;
    it->def   = q->default_value;
    /* step = 0 aparece em drivers imperfeitos. Como o ajuste é em percentual, usar 1 é seguro. */
    it->step  = q->step ? q->step : 1;
}

/*
 * Lê o valor atual de um controle no driver. Usa VIDIOC_G_EXT_CTRLS (64 bits) e, em
 * kernel antigo, VIDIOC_G_CTRL. Devolve 0 e o valor em *raw, ou -1 com errno.
 */
static int hw_get(int fid, const v4l2ctl_item *it, __s64 *raw) {
    struct v4l2_ext_control  c;
    struct v4l2_ext_controls ec;
    struct v4l2_control      old;

    CLEAR(c);  CLEAR(ec);
    c.id = it->id;
    ec.which = V4L2_CTRL_WHICH_CUR_VAL;
    ec.count = 1;
    ec.controls = &c;
    if (0 == xioctl(fid, VIDIOC_G_EXT_CTRLS, &ec)) {
        *raw = (it->type == V4L2_CTRL_TYPE_INTEGER64) ? (__s64)c.value64 : (__s64)c.value;
        return 0;
    }
    if (errno != ENOTTY)
        return -1;

    CLEAR(old);
    old.id = it->id;
    if (0 != xioctl(fid, VIDIOC_G_CTRL, &old))
        return -1;
    *raw = old.value;
    return 0;
}

/* Grava um valor no driver (mesma estratégia de hw_get). Devolve 0 ou -1 com errno. */
static int hw_set(int fid, const v4l2ctl_item *it, __s64 raw) {
    struct v4l2_ext_control  c;
    struct v4l2_ext_controls ec;
    struct v4l2_control      old;

    CLEAR(c);  CLEAR(ec);
    c.id = it->id;
    if (it->type == V4L2_CTRL_TYPE_INTEGER64) c.value64 = raw;
    else                                      c.value   = (__s32)raw;
    ec.which = V4L2_CTRL_WHICH_CUR_VAL;
    ec.count = 1;
    ec.controls = &c;
    if (0 == xioctl(fid, VIDIOC_S_EXT_CTRLS, &ec))
        return 0;
    if (errno != ENOTTY)
        return -1;

    CLEAR(old);
    old.id = it->id;
    old.value = (__s32)raw;
    return xioctl(fid, VIDIOC_S_CTRL, &old);
}

/* O índice 'idx' de menu existe neste driver? (menus podem ter buracos: itens desativados) */
static int menu_valid(int fid, const v4l2ctl_item *it, __s64 idx) {
    struct v4l2_querymenu qm;

    if (idx < it->min || idx > it->max)
        return 0;
    CLEAR(qm);
    qm.id = it->id;
    qm.index = (__u32)idx;
    return 0 == xioctl(fid, VIDIOC_QUERYMENU, &qm);
}

/*
 * Procura o item de menu válido mais próximo de 'target'.
 *   dir = 0 : nos dois sentidos (empate vai para o menor índice)
 *   dir > 0 : só de 'target' para cima        dir < 0 : só de 'target' para baixo
 * Devolve 0 e o índice em *out, ou -1 se não existe nenhum.
 */
static int menu_snap(int fid, const v4l2ctl_item *it, __s64 target, int dir, __s64 *out) {
    __s64 span = it->max - it->min;
    __s64 d;

    for (d = 0; d <= span; d++) {
        if (dir <= 0 && menu_valid(fid, it, target - d)) { *out = target - d; return 0; }
        if (dir >= 0 && menu_valid(fid, it, target + d)) { *out = target + d; return 0; }
    }
    return -1;
}

/* Valida cs e idx; devolve o item ou NULL (e preenche *err com o código a devolver). */
static v4l2ctl_item *item_at(v4l2ctl_set *cs, int idx, int *err) {
    if (!cs)                              { *err = V4L2CTL_ERR_ARG;   return NULL; }
    if (idx < 0 || idx >= cs->n)          { *err = V4L2CTL_ERR_INDEX; return NULL; }
    *err = 0;
    return &cs->it[idx];
}

/* ========================================================================== */
/*  Ciclo de vida                                                             */
/* ========================================================================== */

/* Zera a tabela: nenhum item, nenhum descritor associado. Deve ser chamada antes do primeiro uso. */
void v4l2ctl_clear(v4l2ctl_set *cs) {
    if (!cs) return;
    memset(cs, 0, sizeof(*cs));
    cs->fid = -1;
}

/*
 * Substitui a antiga populate_controls_table(). Percorre TODOS os controles do driver
 * e guarda na tabela 'cs' os que têm tipo tratável. Diferenças em relação à versão antiga:
 *   - não usa variáveis globais (cada câmera passa a sua própria tabela);
 *   - controles somente leitura/inativos ficam na tabela (marcados em 'flags'), pois
 *     ainda podem ser lidos; v4l2ctl_is_writable() diz se aceitam gravação;
 *   - valores de 64 bits são lidos sem truncar; step = 0 vira 1.
 * Devolve quantos itens foram guardados (>= 0) ou um código de erro (< 0).
 */
int v4l2ctl_populate(v4l2ctl_set *cs, int fid) {
    struct v4l2_query_ext_ctrl q;
    __u32 next = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    int legacy = 0;

    if (!cs || fid < 0)
        return V4L2CTL_ERR_ARG;

    v4l2ctl_clear(cs);
    cs->fid = fid;

    for (;;) {
        /* Pede o controle SEGUINTE ao último encontrado (o driver devolve em ordem de id). */
        CLEAR(q);
        q.id = next;
        if (!legacy && 0 != xioctl(fid, VIDIOC_QUERY_EXT_CTRL, &q)) {
            if (errno == ENOTTY && cs->n == 0) { legacy = 1; continue; }   /* kernel antigo */
            break;                                                          /* EINVAL = fim da lista */
        }
        if (legacy) {
            struct v4l2_queryctrl qc;
            CLEAR(qc);
            qc.id = next & ~V4L2_CTRL_FLAG_NEXT_COMPOUND;                   /* só NEXT_CTRL no modo antigo */
            if (0 != xioctl(fid, VIDIOC_QUERYCTRL, &qc))
                break;
            q.id = qc.id;  q.type = qc.type;  q.flags = qc.flags;
            q.minimum = qc.minimum;  q.maximum = qc.maximum;
            q.step = (__u64)qc.step;  q.default_value = qc.default_value;
            memcpy(q.name, qc.name, sizeof(q.name) - 1);     /* q já foi zerado: o terminador fica garantido */
        }

        next = q.id | V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;   /* prepara a próxima volta */

        if ((q.flags & V4L2_CTRL_FLAG_DISABLED) || !type_supported(q.type))
            continue;                                   /* desativado pelo driver ou tipo sem faixa útil */
        if (cs->n >= V4L2CTL_MAX_ITEMS) {               /* tabela cheia: só contabiliza */
            cs->dropped++;
            continue;
        }

        v4l2ctl_item *it = &cs->it[cs->n];
        fill_item(it, &q);

        /* Valor atual direto do hardware; se não puder ser lido usa o valor de fábrica. */
        if (it->type == V4L2_CTRL_TYPE_BUTTON || (it->flags & V4L2_CTRL_FLAG_WRITE_ONLY) ||
            0 != hw_get(fid, it, &it->cur))
            it->cur = it->def;

        cs->n++;
    }
    return cs->n;
}

/*
 * Relê flags, faixa e valor de todos os itens. Use depois de alterar um controle que
 * liga/desliga outros (ex.: exposição automática ativa/inativa a exposição manual).
 */
int v4l2ctl_refresh_all(v4l2ctl_set *cs) {
    struct v4l2_query_ext_ctrl q;
    int i;

    if (!cs) return V4L2CTL_ERR_ARG;
    for (i = 0; i < cs->n; i++) {
        v4l2ctl_item *it = &cs->it[i];
        if (0 == query_one(cs->fid, it->id, &q))
            fill_item(it, &q);                          /* atualiza flags/faixa que o driver mudou */
        if (it->type != V4L2_CTRL_TYPE_BUTTON && !(it->flags & V4L2_CTRL_FLAG_WRITE_ONLY))
            hw_get(cs->fid, it, &it->cur);              /* falha aqui mantém o último valor conhecido */
    }
    return cs->n;
}

/* ========================================================================== */
/*  Consulta                                                                  */
/* ========================================================================== */

/* Devolve o índice do controle com esse V4L2_CID_* ou -1. */
int v4l2ctl_find_id(const v4l2ctl_set *cs, __u32 id) {
    int i;
    if (!cs) return -1;
    for (i = 0; i < cs->n; i++)
        if (cs->it[i].id == id) return i;
    return -1;
}

/*
 * Busca por nome, sem diferenciar maiúsculas de minúsculas. Primeiro tenta o nome
 * IGUAL ("brightness"), depois o que CONTÉM o texto ("exposure" acha "Exposure, Auto").
 * Devolve o índice ou -1.
 */
int v4l2ctl_find_name(const v4l2ctl_set *cs, const char *name) {
    int i;
    if (!cs || !name || !*name) return -1;
    for (i = 0; i < cs->n; i++)
        if (0 == strcasecmp(cs->it[i].name, name)) return i;
    for (i = 0; i < cs->n; i++)
        if (strcasestr(cs->it[i].name, name)) return i;
    return -1;
}

/*
 * 1 se o controle aceita gravação: não é somente leitura, nem desativado, nem preso por
 * outro controle (GRABBED), e tem faixa para variar (ou é um botão). Controles INATIVOS
 * aceitam gravação (o valor só passa a valer quando o controle que os inativa mudar).
 */
int v4l2ctl_is_writable(const v4l2ctl_item *it) {
    if (!it) return 0;
    if (it->flags & (V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_GRABBED))
        return 0;
    return it->type == V4L2_CTRL_TYPE_BUTTON || it->min < it->max;
}

/* Nome curto do tipo, para listagens. */
const char *v4l2ctl_type_name(__u32 type) {
    switch (type) {
        case V4L2_CTRL_TYPE_INTEGER:      return "int";
        case V4L2_CTRL_TYPE_BOOLEAN:      return "bool";
        case V4L2_CTRL_TYPE_MENU:         return "menu";
        case V4L2_CTRL_TYPE_INTEGER_MENU: return "intmenu";
        case V4L2_CTRL_TYPE_INTEGER64:    return "int64";
        case V4L2_CTRL_TYPE_BUTTON:       return "button";
        default:                          return "?";
    }
}

/* ========================================================================== */
/*  Conversão percentual <-> valor real (funções puras)                       */
/* ========================================================================== */

/* Valor real -> percentual da faixa [min, max]. Faixa vazia devolve 0. Resultado limitado a 0..100. */
double v4l2ctl_raw_to_pct(const v4l2ctl_item *it, __s64 raw) {
    long double span, p;
    if (!it) return 0.0;
    span = (long double)it->max - (long double)it->min;
    if (span <= 0) return 0.0;
    p = ((long double)raw - (long double)it->min) * 100.0L / span;
    if (p < 0)   p = 0;
    if (p > 100) p = 100;
    return (double)p;
}

/*
 * Percentual -> valor real. O percentual é limitado a 0..100 e o resultado é
 * ENCAIXADO no passo do controle (min + k*step), sem passar de max.
 * Ex.: faixa 0..255, passo 1: 50 % -> 128 (127,5 arredondado).
 */
__s64 v4l2ctl_pct_to_raw(const v4l2ctl_item *it, double pct) {
    long double span, r, step;
    __s64 k, raw;

    if (!it) return 0;
    span = (long double)it->max - (long double)it->min;
    if (span <= 0) return it->min;
    if (pct != pct) pct = 0;                        /* NaN vira 0 % */
    if (pct < 0)    pct = 0;
    if (pct > 100)  pct = 100;

    step = it->step ? (long double)it->step : 1.0L;
    r = pct / 100.0L * span;                        /* deslocamento a partir de min */
    k = rnd_ll(r / step);                           /* quantos passos */
    raw = it->min + (__s64)(k * (__s64)step);
    while (raw > it->max) raw -= (__s64)step;       /* arredondar para cima pode passar do máximo */
    if (raw < it->min) raw = it->min;
    return raw;
}

/* ========================================================================== */
/*  Valor real (cru)                                                          */
/* ========================================================================== */

/* Lê o valor atual no driver, guarda em it[idx].cur e devolve em *raw (pode ser NULL). */
int v4l2ctl_get_raw(v4l2ctl_set *cs, int idx, __s64 *raw) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    __s64 v;

    if (!it) return err;
    if (it->type == V4L2_CTRL_TYPE_BUTTON || (it->flags & V4L2_CTRL_FLAG_WRITE_ONLY)) {
        v = it->cur;                                /* não pode ser lido: devolve o último conhecido */
    } else {
        if (0 != hw_get(cs->fid, it, &v))
            return fail_errno(cs, errno);
        it->cur = v;
    }
    if (raw) *raw = v;
    return V4L2C_OK;
}

/*
 * Grava um valor real: limita a [min, max], encaixa no passo, (em menus) encaixa no item
 * válido mais próximo, grava e RELÊ o valor, porque o driver pode ter ajustado.
 */
int v4l2ctl_set_raw(v4l2ctl_set *cs, int idx, __s64 raw) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    __s64 back;

    if (!it) return err;
    if (it->type == V4L2_CTRL_TYPE_BUTTON)
        return v4l2ctl_trigger(cs, idx);
    if (!v4l2ctl_is_writable(it))
        return V4L2CTL_ERR_NOTSET;

    /* 1) limita à faixa e encaixa no passo (min + k*step) */
    if (raw < it->min) raw = it->min;
    if (raw > it->max) raw = it->max;
    {
        __s64 st = (__s64)it->step;
        __s64 k = rnd_ll((long double)(raw - it->min) / (long double)st);
        raw = it->min + k * st;
        while (raw > it->max) raw -= st;
    }

    /* 2) em menus, o índice precisa existir de fato */
    if (type_is_menu(it->type)) {
        __s64 ok;
        if (0 != menu_snap(cs->fid, it, raw, 0, &ok))
            return V4L2C_ERR_UNSUPPORTED;           /* menu sem nenhum item válido */
        raw = ok;
    }

    /* 3) grava; depois relê para registrar o que o driver realmente aplicou */
    if (0 != hw_set(cs->fid, it, raw))
        return fail_errno(cs, errno);
    it->cur = raw;
    if (!(it->flags & V4L2_CTRL_FLAG_WRITE_ONLY) && 0 == hw_get(cs->fid, it, &back))
        it->cur = back;
    return V4L2C_OK;
}

/* ========================================================================== */
/*  Percentual: a interface recomendada                                       */
/* ========================================================================== */

/* Lê o controle e devolve o valor atual como percentual (0..100) da faixa. */
int v4l2ctl_get_pct(v4l2ctl_set *cs, int idx, double *pct) {
    __s64 raw;
    int r = v4l2ctl_get_raw(cs, idx, &raw);
    if (r < 0) return r;
    if (pct) *pct = v4l2ctl_raw_to_pct(&cs->it[idx], raw);
    return V4L2C_OK;
}

/* Ajusta o controle para 'pct' por cento da faixa (limitado a 0..100). Botões: use v4l2ctl_trigger. */
int v4l2ctl_set_pct(v4l2ctl_set *cs, int idx, double pct) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    if (!it) return err;
    if (it->type == V4L2_CTRL_TYPE_BUTTON)
        return V4L2CTL_ERR_NOTSET;                  /* botão não tem valor: use v4l2ctl_trigger */
    return v4l2ctl_set_raw(cs, idx, v4l2ctl_pct_to_raw(it, pct));
}

/*
 * Soma 'delta_pct' (pode ser negativo) ao valor ATUAL, relido do driver. Garante que o
 * controle ande pelo menos UM passo (ou um item de menu) quando delta != 0, mesmo que o
 * delta seja menor que um passo - senão um "+1 %" num controle de 0..10 nunca sairia do lugar.
 * Nos limites (0 % ou 100 %) o valor simplesmente não muda. Devolve o novo percentual em *new_pct.
 */
int v4l2ctl_adjust_pct(v4l2ctl_set *cs, int idx, double delta_pct, double *new_pct) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    __s64 cur, target;
    int dir;
    int r;

    if (!it) return err;
    if (it->type == V4L2_CTRL_TYPE_BUTTON) return V4L2CTL_ERR_NOTSET;
    r = v4l2ctl_get_raw(cs, idx, &cur);
    if (r < 0) return r;

    if (delta_pct != 0.0) {
        dir = (delta_pct > 0) ? 1 : -1;
        target = v4l2ctl_pct_to_raw(it, v4l2ctl_raw_to_pct(it, cur) + delta_pct);

        if (type_is_menu(it->type)) {
            /* Em menu, procura o próximo item válido NO SENTIDO do ajuste. */
            __s64 start = (target == cur) ? cur + dir : target;
            int sd = (target > cur) ? 1 : (target < cur) ? -1 : dir;
            __s64 found;
            target = (0 == menu_snap(cs->fid, it, start, sd, &found)) ? found : cur;
        } else if (target == cur) {
            /* O delta caiu dentro do mesmo passo: anda um passo inteiro, se ainda couber. */
            __s64 moved = cur + dir * (__s64)it->step;
            if (moved >= it->min && moved <= it->max) target = moved;
        }

        if (target != cur) {
            r = v4l2ctl_set_raw(cs, idx, target);
            if (r < 0) return r;
        }
    }
    if (new_pct) *new_pct = v4l2ctl_raw_to_pct(it, it->cur);
    return V4L2C_OK;
}

/* Volta o controle ao valor de fábrica informado pelo driver. */
int v4l2ctl_reset_default(v4l2ctl_set *cs, int idx) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    if (!it) return err;
    return v4l2ctl_set_raw(cs, idx, it->def);
}

/*
 * Propósito: um controle é "de modo" (liga/desliga ou escolha), e não "de valor"?
 * Os de modo (ex.: exposição automática, balanço de branco automático) tornam inativos
 * os controles de valor que dependem deles, por isso são resetados POR ÚLTIMO.
 */
static int is_mode_control(const v4l2ctl_item *it) {
    return it->type == V4L2_CTRL_TYPE_BOOLEAN || type_is_menu(it->type);
}

/* Propósito: resetar, numa passada, os controles gravaveis do grupo pedido (modo ou valor). */
static void reset_pass(v4l2ctl_set *cs, int modes) {
    int i;

    for (i = 0; i < cs->n; i++) {
        v4l2ctl_item *it = &cs->it[i];
        if (it->type == V4L2_CTRL_TYPE_BUTTON)       continue;      /* botão não tem valor para repor */
        if (is_mode_control(it) != modes)            continue;      /* só o grupo desta passada */
        if (!v4l2ctl_is_writable(it))                continue;      /* inativo/preso: tenta de novo na próxima passada */
        if (it->cur == it->def)                      continue;      /* já está no valor de fábrica */
        v4l2ctl_set_raw(cs, i, it->def);
    }
}

/*
 * Propósito: devolver TODOS os controles ao valor de fábrica informado pelo driver.
 * A ordem importa: se o modo automático fosse reposto antes, os controles de valor
 * (ex.: tempo de exposição) ficariam inativos e não poderiam mais ser repostos.
 *   passada 1 : controles de valor      (ainda graváveis no modo atual)
 *   passada 2 : controles de modo       (liga/desliga e menus)
 *   passada 3 : controles de valor      (os que só ficaram graváveis após a mudança de modo)
 * Antes de cada passada relê valores e flags do driver.
 * Devolve quantos controles graváveis NÃO estão no valor de fábrica ao final (0 = tudo certo).
 */
int v4l2ctl_reset_all(v4l2ctl_set *cs) {
    int i, left = 0;

    if (!cs) return V4L2CTL_ERR_ARG;
    if (cs->n == 0) return 0;

    v4l2ctl_refresh_all(cs);
    reset_pass(cs, 0);                  /* valores */
    v4l2ctl_refresh_all(cs);
    reset_pass(cs, 1);                  /* modos */
    v4l2ctl_refresh_all(cs);
    reset_pass(cs, 0);                  /* valores liberados pelo novo modo */
    v4l2ctl_refresh_all(cs);

    for (i = 0; i < cs->n; i++) {
        const v4l2ctl_item *it = &cs->it[i];
        if (it->type != V4L2_CTRL_TYPE_BUTTON && v4l2ctl_is_writable(it) && it->cur != it->def)
            left++;
    }
    return left;
}

/* Dispara um controle do tipo BOTÃO (ex.: "reiniciar foco"). Outros tipos devolvem V4L2CTL_ERR_ARG. */
int v4l2ctl_trigger(v4l2ctl_set *cs, int idx) {
    int err;
    v4l2ctl_item *it = item_at(cs, idx, &err);
    if (!it) return err;
    if (it->type != V4L2_CTRL_TYPE_BUTTON) return V4L2CTL_ERR_ARG;
    if (!v4l2ctl_is_writable(it)) return V4L2CTL_ERR_NOTSET;
    if (0 != hw_set(cs->fid, it, 1))                /* o valor é ignorado pelo driver; o ato de gravar dispara */
        return fail_errno(cs, errno);
    return V4L2C_OK;
}

/* ========================================================================== */
/*  Menus                                                                     */
/* ========================================================================== */

/*
 * Escreve em 'out' o texto do item 'value' (índice) do menu 'idx'. Em menu de inteiros
 * escreve o número. Devolve 0, ou < 0 se o controle não é menu ou o índice não existe.
 */
int v4l2ctl_menu_name(const v4l2ctl_set *cs, int idx, __s64 value, char *out, size_t outsz) {
    struct v4l2_querymenu qm;
    const v4l2ctl_item *it;

    if (!cs || !out || outsz == 0) return V4L2CTL_ERR_ARG;
    if (idx < 0 || idx >= cs->n)   return V4L2CTL_ERR_INDEX;
    it = &cs->it[idx];
    if (!type_is_menu(it->type))   return V4L2CTL_ERR_ARG;

    CLEAR(qm);
    qm.id = it->id;
    qm.index = (__u32)value;
    if (0 != xioctl(cs->fid, VIDIOC_QUERYMENU, &qm))
        return V4L2C_ERR_IOCTL;

    if (it->type == V4L2_CTRL_TYPE_MENU)
        snprintf(out, outsz, "%s", (const char *)qm.name);
    else
        snprintf(out, outsz, "%lld", (long long)qm.value);
    return V4L2C_OK;
}
