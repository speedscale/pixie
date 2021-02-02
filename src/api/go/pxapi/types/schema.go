package types

// TableMetadata contains the table metadata state.
type TableMetadata struct {
	// Name of the TableMetadata.
	Name string
	// ColInfo has the array index schemas of each column.
	ColInfo []ColSchema
	// ColIdxByName has the mapping from column names to column indices.
	ColIdxByName map[string]int64
}

// IndexOf returns the index of a column by name. -1 is returned if the column does not exist.
func (t *TableMetadata) IndexOf(colName string) int64 {
	idx, ok := t.ColIdxByName[colName]
	if !ok {
		return -1
	}
	return idx
}

// ColSchema has the per column schema.
type ColSchema struct {
	// Name of the column.
	Name string
	// Type of the column.
	Type DataType
	// SemanticType of the column.
	SemanticType SemanticType
}

// Record stores information about a single record.
type Record struct {
	// Data is the array index type erased values.
	Data []Datum
	// TableMetadata stores a pointer to underlying table metadata.
	TableMetadata *TableMetadata
}

// GetDatum returns the value of the given column.
func (r *Record) GetDatum(colName string) Datum {
	idx := r.TableMetadata.IndexOf(colName)
	if idx < 0 {
		return nil
	}
	return r.Data[idx]
}

// GetDatumByIdx returns the data at a given column index.
func (r *Record) GetDatumByIdx(idx int64) Datum {
	return r.Data[idx]
}