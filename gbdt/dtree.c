/* ========================================================
 *   Copyright (C) 2016 All rights reserved.
 *
 *   filename : dtree.c
 *   author   : liuzhiqiangruc@126.com
 *   date     : 2016-02-26
 *   info     : implementation for decision_tree model
 * ======================================================== */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "dtree.h"

#define  DBL_MAX 1.1e10

struct _d_tree {
    int n;                          /* instance num in this node  */
    int leaf;                       /* 0:noleaf, 1:leaf, 2:calculating, 3:calculated */
    int depth;                      /* depth of this node         */
    int attr;                       /* if not leaf, split attr    */
    double attr_val;                /* split val of this attr     */
    double sg;                      /* sum of 1-gradient          */
    double sh;                      /* sum of 2-gradient          */
    double wei;                     /* additive model value       */
    double loss;                    /* loss value of this node    */
    double gain;                    /* gain of best split         */
    struct _d_tree * child[2];      /* splited children nodes     */
};

static void scan_tree(DTD * ts, DTree * t, DTree ** inst_nodes, int n, int m){
    int i, id, rowid, l_c, r_c;
    l_c = 0;
    r_c = m;
    for (i = 0; i < ts->l[t->attr]; i++){
        id = i + ts->cl[t->attr];
        rowid = ts->ids[id];
        if (inst_nodes[rowid] == t) {
            if (ts->bin == 1 || (ts->bin == 0 && ts->vals[id] >= t->attr_val)){
                l_c += 1;
                r_c -= 1;
                inst_nodes[rowid] = t->child[0];
            }
        }
    }
    for (i = 0; i < n; i++){
        if (inst_nodes[i] == t){
            inst_nodes[i] = t->child[1];
        }
    }
    if (l_c > 0 && t->child[0]->leaf == 0) {
        scan_tree(ts, t->child[0], inst_nodes, n, l_c);
    }
    if (r_c > 0 && t->child[1]->leaf == 0) {
        scan_tree(ts, t->child[1], inst_nodes, n, r_c);
    }
}

static void init_child(DTree * t){
    DTree * le = (DTree *)malloc(sizeof(DTree));
    DTree * ri = (DTree *)malloc(sizeof(DTree));
    memset(le, 0, sizeof(DTree));
    memset(ri, 0, sizeof(DTree));
    t->child[0]  = le;
    t->child[1]  = ri;
    le->leaf     = ri->leaf  = 2; // calculating status
    le->depth    = ri->depth = t->depth + 1;
}

static void update_child(DTree * t, int k, double nr, double wr, double v){
    double l_loss, r_loss, gain;
    DTree *le = t->child[0], *rt = t->child[1];
    l_loss = -0.5 * le->sg * le->sg / (le->sh + wr) + nr;
    r_loss = -0.5 * (t->sg - le->sg) * (t->sg - le->sg) / (t->sh - le->sh + wr) + nr;
    gain   = t->loss - l_loss - r_loss;
    if (gain > t->gain){
        // update t
        t->gain = gain;
        t->attr = k;
        t->attr_val = (v + le->attr_val) / 2.0;

        // update left child, no need to do this
        // le->loss = l_loss;
        // le->wei  = -1.0 * le->sg / (le->sh + wr);

        // update right child
        rt->n    = t->n - le->n;
        rt->loss = r_loss;
        rt->sg   = t->sg - le->sg;
        rt->sh   = t->sh - le->sh;
        rt->wei  = -1.0 * rt->sg / (rt->sh + wr);
    }
}

static void fix_child(DTree * t, double nr, double wr){
    DTree *le, *rt;
    le = t->child[0];
    rt = t->child[1];
    le->sg   = t->sg - rt->sg;
    le->sh   = t->sh - rt->sh;
    le->n    = t->n  - rt->n;
    le->wei  = -1.0 * le->sg / (le->sh + wr);
    le->loss = -0.5 * le->sg * le->sg / (le->sh + wr) + nr;
    le->leaf = rt->leaf = 3;
}

static int tree_grow(DTD * ds
                , DTree ** leaf_nodes
                , int    * inst_nodes
                , double * g, double *h
                , double nr , double wr
                , int l, int s, int d) {
    // tree grow progress
    int i, j, o, r;
    double v = 0.0;
    DTree *t = NULL, *le = NULL;
    // scan ds via column
    for (i = 0; i < ds->col; i++) {
        o = ds->cl[i];
        for (j = 0; j < ds->l[i]; j++){
            r  = ds->ids[o + j];
            t  = leaf_nodes[inst_nodes[r]];
            if (NULL == t->child[0]){
                init_child(t);
            }
            le = t->child[0];
            if (t->depth < d && t->n >= (s << 1) && le->leaf == 2){
                le->attr_val = 1.0; // default binary feature
                if (0 == ds->bin) {
                    v = ds->vals[o + j];
                    if (v < le->attr_val && le->attr_val < DBL_MAX && le->n >= s){
                        update_child(t, i, nr, wr, v);
                    }
                    le->attr_val = v;
                }
                le->n  += 1;
                le->sg += g[r];
                le->sh += h[r];
            }
        }
        for (j = 0; j < l; j++){
            t = leaf_nodes[j];
            if (t->child[0]->leaf == 2){
                update_child(t, i, nr, wr, 0.0);
                // reset left child status
                t->child[0]->n = 0;
                t->child[0]->sg = t->child[0]->sh = 0.0;
            }
        }
    }

    // select best split
    i = -1;
    v = 0.0;
    for (j = 0; j < l; j++){
        t = leaf_nodes[j];
        if (t->child[0]->leaf == 2){
            fix_child(t, nr, wr);
        }
        if (t->gain > v){
            v = t->gain;
            i = j;
        }
    }
    leaf_nodes[i]->leaf = 0;
    leaf_nodes[i]->child[0]->leaf = leaf_nodes[i]->child[1]->leaf = 1;
    return i;
}


static DTree * init_root(double * g, double * h, int n, double nr, double wr){
    int i;
    DTree * t = (DTree *)malloc(sizeof(DTree));
    memset(t, 0, sizeof(DTree));
    t->n = n;
    t->depth = 0;
    t->leaf = 1;
    for (i = 0; i < n; i++){
        t->sg += g[i];
        t->sh += h[i];
    }
    t->wei  = -1.0 * t->sg / (t->sh + wr);
    t->loss = -0.5 * t->sg * t->sg / (t->sh + wr) + nr;
    return t;
}

DTree * generate_dtree(DTD * ds      /* dataset for build tree */
                     , double * F    /* current f vector       */
                     , double * g    /* current gradient vec   */
                     , double * h    /* current hessian  vec   */
                     , double nr     /* node regulizatin       */
                     , double wr     /* weight regulization    */
                     , int n         /* number of instances    */
                     , int s         /* min instance each node */
                     , int d         /* max depth of tree      */
                     , int m){       /* max leaf nodes         */
    int i, k, o, l;
    if (m < 2)
        return NULL;
    DTree ** leaf_nodes = (DTree**)malloc(sizeof(DTree*) * m);
    memset(leaf_nodes, 0, sizeof(DTree*) * m);
    int * inst_nodes = (int*)malloc(sizeof(int) * n);
    memset(inst_nodes, 0, sizeof(int) * n);
    DTree * t = init_root(g, h, n, nr, wr);
    l = 0;
    for (i = 0; i < n; i++){
        inst_nodes[i] = l;
    }
    leaf_nodes[l++] = t;
    while (l < m){
        if(-1 == (k = tree_grow(ds, leaf_nodes, inst_nodes, g, h, nr, wr, l, s, d))){
            break;
        }
        DTree * tmp = leaf_nodes[k];
        o = ds->cl[tmp->attr];
        for (i = 0; i < ds->l[tmp->attr]; i++){
            if (inst_nodes[ds->ids[o + i]] == k){
                if ((1 == ds->bin) || (0 == ds->bin && ds->vals[o + i] >= tmp->attr_val)){
                    inst_nodes[ds->ids[o + i]] = l;
                }
            }
        }
        leaf_nodes[l++] = tmp->child[0];
        leaf_nodes[k] = tmp->child[1];
    }
    if (t->leaf == 0){
        for (i = 0; i < n; i++){
            F[i] = leaf_nodes[inst_nodes[i]]->wei;
        }
    }
    else{
        free(t); t = NULL;
    }
    free(leaf_nodes);    leaf_nodes = NULL;
    free(inst_nodes);    inst_nodes = NULL;
    return t;
}

void free_dtree(DTree * t){
    if (t){
        if(t->child[0]){
            free_dtree(t->child[0]);
            t->child[0] = NULL;
        }
        if (t->child[1]){
            free_dtree(t->child[1]);
            t->child[1] = NULL;
        }
        free(t);
    }
}

double * eval_tree(DTD * ts, DTree * t, double * F, int n){
    int i;
    DTree ** inst_nodes = NULL;
    if (ts && F && t && t->leaf == 0 && n > 0) {
        inst_nodes = (DTree**)malloc(sizeof(DTree *) * n);
        for (i = 0; i < n; i++){
            inst_nodes[i] = t;
        }
        scan_tree(ts, t, inst_nodes, n, n);
        for (i = 0; i < n; i++){
            F[i] = inst_nodes[i]->wei;
        }
        free(inst_nodes); inst_nodes = NULL;
    }
    return F;
}

void save_dtree(DTree * t, char * out_file, char (*id_map)[FKL]){
    if (!t){
        return;
    }
    FILE * fp = NULL;
    if (NULL == (fp = fopen(out_file, "w"))){
        fprintf(stderr, "save out file \"%s\"\n", out_file);
        return;
    }
    // max 1000 leaf_nodes and 999 non_leaf nodes in tree
    DTree ** ts = (DTree **)malloc(sizeof(void *) * 1999);
    memset(ts, 0, sizeof(void*) * 1999);
    int i, l, c1, c2;
    i = l = 0;
    ts[l++] = t;
    fprintf(fp, "n\tleaf\tAttr\tAttr_val\tNode_wei\tNode_loss\tleft\tright\n");
    do {
        DTree * ct = ts[i];
        c1 = c2 = 0;
        // if is not leaf, push two children into ts
        if (ct->leaf == 0){
            c1 = l; ts[l++] = ct->child[0];
            c2 = l; ts[l++] = ct->child[1];
            fprintf(fp, "%d\t%d\t%s\t%.3f\t%.3f\t%.3f\t%d\t%d\n"          \
                      , ct->n,   ct->leaf, id_map[ct->attr], ct->attr_val \
                      , ct->wei, ct->loss, c1, c2);
        }
        else{
            fprintf(fp, "%d\t%d\tNone\tNone\t%.3f\t%.3f\t%d\t%d\n"    \
                      , ct->n, ct->leaf, ct->wei, ct->loss, c1, c2);
        }
        i += 1;
    } while (i < l && l <= 1997);
    fclose(fp);
    free(ts);
    ts = NULL;
}
