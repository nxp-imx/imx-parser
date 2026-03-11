/*****************************************************************************
 * flist.h
 *
 * Copyright (c) 2005 Freescale Semiconductor Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 * Description: List data structure manipulation.
 * It more like a stack, new added nodes will be inserted at the head.
 *
 ****************************************************************************/

#ifndef FLIST_H
#define FLIST_H

/***** <Global Macros> ****************************/
#define FLIST_LINK_ITEM(type) \
    type* pPrev;              \
    type* pNext

#define FLIST_ITEM_INIT(pItem) \
    do {                       \
        (pItem)->pPrev = 0;    \
        (pItem)->pNext = 0;    \
    } while (0)

#define FLIST_ADD(pHead, pNew)       \
    do {                             \
        if ((pHead)) {               \
            (pNew)->pNext = (pHead); \
            (pNew)->pPrev = 0;       \
            (pHead)->pPrev = (pNew); \
            (pHead) = (pNew);        \
        } else {                     \
            (pHead) = (pNew);        \
            (pHead)->pPrev = 0;      \
            (pHead)->pNext = 0;      \
        }                            \
    } while (0)

#define FLIST_DELETE(pHead, pItem)                        \
    do {                                                  \
        if ((pItem)->pPrev == 0 && (pItem)->pNext == 0) { \
            (pHead) = 0;                                  \
        } else if ((pItem)->pPrev == 0) {                 \
            (pHead) = (pItem)->pNext;                     \
            (pHead)->pPrev = 0;                           \
            (pItem)->pNext = 0;                           \
        } else if ((pItem)->pNext == 0) {                 \
            (pItem)->pPrev->pNext = 0;                    \
            (pItem)->pPrev = 0;                           \
        } else {                                          \
            (pItem)->pPrev->pNext = (pItem)->pNext;       \
            (pItem)->pNext->pPrev = (pItem)->pPrev;       \
            (pItem)->pPrev = 0;                           \
            (pItem)->pNext = 0;                           \
        }                                                 \
    } while (0)

#define FLIST_FOR_EACH(pHead, pItem) for ((pItem) = (pHead); (pItem) != 0; (pItem) = (pItem)->pNext)

#define FLIST_FOR_EACH_SAFE(pHead, pItem, pTemp)                                           \
    for ((pItem) = (pHead), (pTemp) = (((pItem) == 0) ? 0 : (pItem)->pNext); (pItem) != 0; \
         (pItem) = (pTemp), (pTemp) = (((pTemp) == 0) ? 0 : (pItem)->pNext))

/* find the tail of the list, the latest added node */
#define FLIST_FIND_TAIL(pHead, pItem, pTail)                          \
    pTail = pHead;                                                    \
    for ((pItem) = (pHead); (pItem) != 0; (pItem) = (pItem)->pNext) { \
        pTail = pItem;                                                \
    }

#define FLIST_FOR_EACH_SAFE_REVERSE(pTail, pItem, pTemp)                                   \
    for ((pItem) = (pTail), (pTemp) = (((pItem) == 0) ? 0 : (pItem)->pPrev); (pItem) != 0; \
         (pItem) = (pTemp), (pTemp) = (((pTemp) == 0) ? 0 : (pItem)->pPrev))

#endif /* FLIST_H */

/*=============================== End of File ==============================*/
