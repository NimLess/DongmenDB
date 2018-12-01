//
// Created by Sam on 2018/1/26.
//

#include <utils/utils.h>
#include <iostream>
#include "dongmendb/recordfile.h"

RecordFile::RecordFile(table_info *tableInfo,
                       transaction *tx) {

    this->tableInfo = tableInfo;
    this->tx = tx;
    this->recordPage = NULL;
    this->fileName = NULL;
    this->currentBlkNum = -1;

    char *fileName = new_id_name();
    strcpy(fileName, tableInfo->tableName);
    strcat(fileName, RECORD_FILE_EXT);

    this->fileName = fileName;
    if (transaction_size(tx, this->fileName) == 0) {
        record_file_append_block();
    }
    record_file_moveto( 0);
}

int RecordFile::record_file_close() {
    record_page_close(this->recordPage);
}

int RecordFile::record_file_before_first() {
    record_file_moveto( 0);
}

int RecordFile::record_file_next() {
    while (1) {
        if (record_page_next(this->recordPage)) {
            return 1;
        }
        if (record_file_atlast()) {
            return 0;
        }
        record_file_moveto( this->currentBlkNum + 1);
    }
}

int RecordFile::record_file_atlast() {
    return this->currentBlkNum == transaction_size(this->tx, this->fileName) - 1;
}

int RecordFile::record_file_get_int(const char *fieldName) {
    return record_page_getint(this->recordPage, fieldName);
}

int RecordFile::record_file_get_string(const char *fieldName, char *value) {
    return record_page_getstring(this->recordPage, fieldName, value);
}

int RecordFile::record_file_set_int(const char *fieldName, int value) {
    return record_page_setint(this->recordPage, fieldName, value);
}

int RecordFile::record_file_set_string(const char *fieldName, const char *value) {
    return record_page_setstring(this->recordPage, fieldName, value);
}

int RecordFile::record_file_delete() {
    return record_page_delete(this->recordPage);
}

int RecordFile::record_file_insert() {
    while (!record_page_insert(this->recordPage)) {
        /*逻辑问题*/
        if (record_file_atlast_block()) {
            record_file_append_block();
        }
        record_file_moveto( this->currentBlkNum + 1);
    }
}

int RecordFile::record_file_moveto_recordid(record_id *recordId) {
    record_file_moveto( recordId->blockNum);
    record_page_moveto_id(this->recordPage, recordId->id);
}

int RecordFile::record_file_current_recordid(record_id *recordId) {
    recordId = (record_id *) malloc(sizeof(record_id));
    recordId->id = this->recordPage->currentSlot;
    recordId->blockNum = this->currentBlkNum;
    return 0;
}

int RecordFile::record_file_moveto(int currentBlkNum) {
    if (this->recordPage != NULL) {
        record_page_close(this->recordPage);
    }
    this->currentBlkNum = currentBlkNum;
    disk_block *diskBlock = (disk_block *) malloc(sizeof(disk_block));
    diskBlock->blkNum = currentBlkNum;
    diskBlock->fileName = this->fileName;
    record_page *recordPage = record_page_create(this->tx, this->tableInfo, diskBlock);

    this->recordPage = recordPage;
    return DONGMENDB_OK;
}

int RecordFile::record_file_atlast_block() {
    int size = transaction_size(this->tx, this->fileName) - 1;
    int result = this->currentBlkNum == size;
    return result;
}

int RecordFile::record_file_append_block() {
    transaction_append(this->tx, this->fileName, this->tableInfo);
}

/**
 * from RecordFormatter.format&makeDefaultRecord
 * @param recordFile
 * @param memoryPage
 * @return
 */
int RecordFile::record_file_record_formatter(memory_page *memoryPage) {

}

field_info *field_info_create(enum data_type type, int length, char* fieldName) {

    field_info *fieldInfo = (field_info *) malloc(sizeof(field_info*));
    fieldInfo->type = type;
    fieldInfo->length = length;
    fieldInfo->hashCode = bkdr_hash(fieldName);
    fieldInfo->fieldName = strdup(fieldName);

    return fieldInfo;
};

table_info *table_info_create(const char *tableName, vector<char*> fieldsName,  map<string, field_info*>  *lfields) {

    table_info *tableInfo = (table_info *) malloc(sizeof(table_info));
    tableInfo->tableName = new_id_name();
    strcpy( tableInfo->tableName, tableName);
    tableInfo->fieldsName = fieldsName;
    tableInfo->fields = lfields;

    /*TODO：需要释放*/
//
    std::map<unsigned int, int> *lfs = new std::map<unsigned int, int>();

    tableInfo->recordLen = 0;
    int pos = 0;

    int count = fieldsName.size() - 1;
    for (int i = 0; i <= count; i++) {
        const char* fieldName = fieldsName.at( i);

        field_info *fieldInfo = lfields->find(fieldName)->second;

        (*lfs)[fieldInfo->hashCode]=  pos;
//        lfs->insert(pair<string,integer*>(fieldName, ipos));

        if (fieldInfo->type == DATA_TYPE_CHAR) {
            /*增加字符串长度的存储 位置*/
            pos += fieldInfo->length + INT_SIZE;
        } else if (fieldInfo->type == DATA_TYPE_INT) {
            pos += fieldInfo->length;
        }

    }
    tableInfo->offsets = lfs;
    tableInfo->recordLen = pos;
    return tableInfo;
};

int table_info_free(table_info *tableInfo) {
    /*free hashmap offsets*/
    // free hashmap fields
    // free arraylist fieldsName
    return DONGMENDB_OK;
}

int table_info_offset(table_info *tableInfo, const char *fieldName) {
    unsigned int fid = bkdr_hash(fieldName);
    return tableInfo->offsets->find(fid)->second;
};

record_page *record_page_create(transaction *tx, table_info *tableInfo, disk_block *diskBlock) {
    record_page *recordPage = (record_page *) malloc(sizeof(record_page));
    recordPage->diskBlock = diskBlock;
    recordPage->tx = tx;
    recordPage->tableInfo = tableInfo;
    /*保留一个整型的位置保存slot的状态*/
    recordPage->slotSize = tableInfo->recordLen + INT_SIZE;
    recordPage->currentSlot = -1;
    diskBlock->tableInfo = tableInfo;
    transaction_pin(tx, diskBlock);
    return recordPage;
};

int record_page_close(record_page *recordPage) {
    if (recordPage->diskBlock != NULL) {
        transaction_unpin(recordPage->tx, recordPage->diskBlock);
        recordPage->diskBlock = NULL;
    }
}

int record_page_insert(record_page *recordPage) {
    recordPage->currentSlot = -1;
    if (record_page_searchfor(recordPage, RECORD_PAGE_EMPTY)) {
        int position = record_page_current_pos(recordPage);
        transaction_setint(recordPage->tx, recordPage->diskBlock, position, RECORD_PAGE_INUSE);
        return 1;
    }
    return 0;
}

int record_page_moveto_id(record_page *recordPage, int id) {
    recordPage->currentSlot = id;
}

int record_page_next(record_page *recordPage) {
    record_page_searchfor(recordPage, RECORD_PAGE_INUSE);
};

int record_page_getint(record_page *recordPage, const char *fieldName) {
    int position = record_page_fieldpos(recordPage, fieldName);
    return transaction_getint(recordPage->tx, recordPage->diskBlock, position);
};

int record_page_getstring(record_page *recordPage, const char *fieldName, char *value) {
    int position = record_page_fieldpos(recordPage, fieldName);
    return transaction_getstring(recordPage->tx, recordPage->diskBlock, position, value);
};

int record_page_setint(record_page *recordPage, const char *fieldName, int value) {
    int position = record_page_fieldpos(recordPage, fieldName);
    return transaction_setint(recordPage->tx, recordPage->diskBlock, position, value);
};

int record_page_setstring(record_page *recordPage, const char *fieldName, const char *value) {
    int position = record_page_fieldpos(recordPage, fieldName);
    return transaction_setstring(recordPage->tx, recordPage->diskBlock, position, value);
};

int record_page_delete(record_page *recordPage) {
    int position = record_page_current_pos(recordPage);
    return transaction_setint(recordPage->tx, recordPage->diskBlock, position, RECORD_PAGE_EMPTY);
};

int record_page_searchfor(record_page *recordPage, record_page_status status) {
    recordPage->currentSlot++;
    while (record_page_is_valid_slot(recordPage)) {
        int position = record_page_current_pos(recordPage);
        if (transaction_getint(recordPage->tx, recordPage->diskBlock, position) == status) {
            return 1;
        }
        recordPage->currentSlot++;
    }
    return 0;
};

int record_page_current_pos(record_page *recordPage) {
    return recordPage->currentSlot * recordPage->slotSize;
};

/**
 * 计算字段在page中的位置.
 * 每个page由若干slot构成，每个slot保存一条记录。
 * slotsize=recsize + Intsize,有一个intsize保存slot使用状态。
 *
 * @param recordPage
 * @param fieldName
 * @return
 */
int record_page_fieldpos(record_page *recordPage, const char *fieldName) {
    unsigned int fid = bkdr_hash(fieldName);
    int offset = recordPage->tableInfo->offsets->find(fid)->second;

    return recordPage->currentSlot * recordPage->slotSize + offset + INT_SIZE;
};

int record_page_is_valid_slot(record_page *recordPage) {
    return record_page_current_pos(recordPage) + recordPage->slotSize <= DISK_BOLCK_SIZE;
}