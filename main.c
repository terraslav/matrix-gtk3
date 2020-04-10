#include <locale.h>
#include <gdk/gdkkeysyms.h>
#include <libnotify/notify.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include "main.h"
#include "paint.h"
#include "mafile.h"
#include "puzzle.h"
#include "utils.h"
#include "dialog.h"
#include "library.h"

void print_page();
uint non_area_y_size;

// ************ multithread semapfore ************
static bool thread_semaphore;
void my_threads_init(){
	thread_semaphore = false;
}

void my_threads_enter(){
	while(thread_semaphore)
		usleep(100);
	thread_semaphore = true;
}

void my_threads_leave(){
	thread_semaphore = false;
}

static gboolean quit_program(bool quick){
	if(!quick && 0 > m_save_puzzle(true))	return true;
	if(quick) {
		gtk_widget_destroy(window);
		gtk_main_quit();
		return true;
	}
	if(l_base_stack_top) l_base_save_to_file();				// сохраняю базу
	l_list_mfree(false);
	if(timer_id) g_source_remove(timer_id);
	if(timer_demo) g_source_remove(timer_demo);
//	cairo_destroy(cr);
	save_parameter();
	l_close_jch();
	m_demo_reset_list();
	if (surface) cairo_surface_destroy (surface);
	u_delete(); 													// удаляю буфера отмен
	c_solve_destroy();												// удаляю буфера "решалки"
	c_free_puzzle_buf(atom);
	m_free(atom);
	if(thread_get_library) pthread_cancel(thread_get_library);
	sprintf(last_opened_file, "%s%s", config_path, LOCK_FILE);
	remove(last_opened_file);
	puts(_("Im i`s dead\n"));
	return true;
}

int momento(){
	puts("\n\
********************  Sorry=(  *********************\n\n\
\"matrix\" received signal SIGSEGV, Segmentation fault\n\n\
******************  Saving data...  ****************");
	if(place == memory) {
		l_save_in_base();
//		l_base_save_to_file();
	}
	else m_save_puzzle(false);
	quit_program(true);
	return 0;
}

void posix_death_signal(int signum){	// обработчик исключений SIGSEGV
	momento();
	signal(signum, SIG_DFL);
	exit(3);
}

void posix_abort_signal(int signum){	// обработчик исключений SIGABRT
	quit_program(false);
//	signal(signum, SIG_DFL);
	exit(3);
}
void scrolled_viewport(double x, double y){
	GtkAdjustment* adjustment = gtk_scrolled_window_get_hadjustment((GtkScrolledWindow*)scroll_win);
	double	c		= gtk_adjustment_get_value(adjustment),
			max		= gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment),
			lower	= gtk_adjustment_get_lower(adjustment);
	c += x;
	if(c > max)			c = max;
	else if(c < lower)	c = lower;
	gtk_adjustment_set_value(adjustment, c);
	gtk_scrolled_window_set_hadjustment((GtkScrolledWindow*)scroll_win, adjustment);

	adjustment = gtk_scrolled_window_get_vadjustment((GtkScrolledWindow*)scroll_win);
	c		= gtk_adjustment_get_value(adjustment),
	max		= gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment),
	lower	= gtk_adjustment_get_lower(adjustment);
	c += y;
	if(c > max)			c = max;
	else if(c < lower)	c = lower;
	gtk_adjustment_set_value(adjustment, c);
	gtk_scrolled_window_set_vadjustment((GtkScrolledWindow*)scroll_win, adjustment);
}

void m_update_view(char dir){	// центровка вывода в scroll_win
	byte size = (byte)((char)cell_space + dir);
	if(size < 8 ||  size > 40)return;
	cell_space = size;
	uint	y	= cell_space * atom->y_puzzle_size,
			x	= cell_space * atom->x_puzzle_size;
	gtk_widget_set_size_request(draw_area,x, y);
	gtk_widget_queue_draw(draw_area);
}

void m_export_file(){
	int x,y;
	int width, height;//, depth;
	GdkPixbuf * pixbuf;
	g_assert(NULL != window);
	gdk_window_get_geometry(gtk_widget_get_window(GTK_WIDGET(scroll_win)),
		&x, &y,
		&width, &height);//, &depth);
	if(((width-x) < (atom->x_puzzle_size*cell_space) || (height-y) < (atom->y_puzzle_size*cell_space))){
		d_message(_("Export view"),
			_("Only the part seen on the screen nanograms is exported.\nI recommend to choose suitable ale."), 0);
		return;
	}

	pixbuf = gdk_pixbuf_get_from_surface(surface, atom->x_begin, atom->y_begin,
			atom->x_puzzle_size*cell_space+2, atom->y_puzzle_size*cell_space+2);
	if (pixbuf != NULL){
		char bufp[] = {"/tmp/nanogamm.png"};
		gdk_pixbuf_save(pixbuf, bufp, "png", NULL, NULL);
		g_object_unref(pixbuf);
		char *bufr = malloc(512);
		sprintf(bufr, _("Export view screen to file: %s completed!"), bufp);
		set_status_bar(bufr);
		sprintf(bufr, "xdg-open %s", bufp);
		system(bufr);
		m_free(bufr);
	}
	else d_message(_("Error export"), _("Don't save expotrs"), 0);
}

void m_save_as(){
	int x, y;
	uint i;
	char *buf1 = l_get_name(), *buf;
	if(!buf1) {
		i = strlen(last_opened_file);
		if(i){
			buf = malloc(i + 1);
			strcpy(buf, last_opened_file);
		}
		else {
			buf = malloc(20);
			strcpy(buf, "noname.jch");
		}
	}
	else {
		buf = malloc (strlen(buf1) + 1);
		strcpy(buf, buf1);
	}
	i = strlen(buf);
	x = y = 0;
	for(; i > 0; i--){
		if(buf[y]=='"')
			y++;
		if(buf[y]!=' ')
			 buf[x++] = buf[y];
		else buf[x++] = '_';
		y++;
		}
	ftype = m_get_file_type(last_opened_file);
	if(ftype == mem || ftype == empty)
		 sprintf(last_opened_file, "%s/%s/%s.%s", home_dir, WORK_NAME, buf, "jch");
	else sprintf(last_opened_file, "%s/%s/%s", home_dir, WORK_NAME, buf);
	if(d_save_as_dialog()){
		place = exfile;
		m_save_puzzle_impl();
	}
	m_free(buf);
}

void m_invert(){
	if(atom->isColor){
		d_invert_palette();
	}
	else {
		for(int i=0; i<atom->map_size; i++){
			if(atom->matrix[i] == MARK || atom->matrix[i] == EMPTY)
				 atom->matrix[i] = BLACK;
			else atom->matrix[i] = EMPTY;
		}
		u_digits_updates(false);
		u_after(false);
	}
	c_solve_init(false);
	gtk_widget_queue_draw(draw_area);
}

void m_clear_puzzle(){
	int i, x;
	for(x=0; (size_t)x < atom->map_size; x++)
		if(atom->matrix[x] != EMPTY || shadow_map[x] != EMPTY) break;
	for(i=0; i<atom->top_dig_size*atom->x_size; i++)// очищаю и отметки оцифровки
		atom->top.colors[i] &= COL_MARK_MASK;
	for(i=0; i<atom->left_dig_size*atom->y_size; i++)
		atom->left.colors[i] &= COL_MARK_MASK;
	c_solve_init(false);							// переинициализация "решателя"
	if((size_t)x != atom->map_size){
		memset(shadow_map, EMPTY, atom->map_size);	// очищаю матрицу
		memset(atom->matrix, EMPTY, atom->map_size);
		u_after(false);
	}
	gtk_widget_queue_draw(draw_area);
}

void m_clear_digits(){
	int i;
	for(i=0; i<atom->left_dig_size * atom->y_size; i++)
		atom->left.colors[i] = atom->left.digits[i] = 0;
	for(i=0; i<atom->top_dig_size * atom->x_size; i++)
		atom->top.colors[i] = atom->top.digits[i] = 0;
	atom->digits_status = false;
	u_after(false);
	gtk_widget_queue_draw(draw_area);
}

void m_clear_puzzle_mark(){
	for(int i=0; i<atom->map_size; i++)
		if(atom->matrix[i] == MARK) atom->matrix[i] = ZERO;
	u_after(false);
	gtk_widget_queue_draw(draw_area);
}

void m_clear_digit_mark(){
	int i;
	for(i=0; i<atom->top_dig_size*atom->x_size; i++)
		atom->top.colors[i] &= COL_MARK_MASK;
	for(i=0; i<atom->left_dig_size*atom->y_size; i++)
		atom->left.colors[i] &= COL_MARK_MASK;
	u_after(false);
	gtk_widget_queue_draw(draw_area);
}

void m_normal(){
	int x,y;
//	x = scroll_win->allocation.width;
//	y = scroll_win->allocation.height;
//	x = (int)((double)x * (double)0.9);
//	y = (int)((double)y * (double)0.9);
	x = gtk_widget_get_allocated_width(scroll_win);
	y = gtk_widget_get_allocated_height(scroll_win);
	x = (int)((double)x * (double)0.9);
	y = (int)((double)y * (double)0.9);

	gtk_widget_set_size_request(draw_area, x, y);
}


// Переписываем статусбар **********************************************
void set_status_bar(char *buff){
	gint context_id = gtk_statusbar_get_context_id(
                       GTK_STATUSBAR (statusbar), "Statusbar example");
	gtk_statusbar_remove_all (GTK_STATUSBAR (statusbar), context_id);
	gtk_statusbar_push (GTK_STATUSBAR (statusbar),
								GPOINTER_TO_INT(context_id), buff);
}

void clear_status_bar(){
	gint context_id = gtk_statusbar_get_context_id(
                       GTK_STATUSBAR (statusbar), "Statusbar example");
	gtk_statusbar_remove_all (GTK_STATUSBAR (statusbar), context_id);
}

gboolean scroll_event(GtkWidget *a,GdkEventScroll *event,void *b){
	if(atom->curs->status){
		set_digit_value(event->direction == GDK_SCROLL_DOWN ? -3 : -2);
		return true;
	}
	m_update_view(event->direction == GDK_SCROLL_DOWN ? -1 : 1);
	return true;
}

gboolean key_press (GtkWidget *widget, GdkEventKey *event, gpointer user_data){
	direct vect;
	if(atom->curs->status){
		if(event->keyval >= GDK_KP_0 && event->keyval <= GDK_KP_9)
			{set_digit_value(event->keyval - GDK_KP_0); return true;}
		else if(event->keyval >= GDK_0 && event->keyval <= GDK_9)
			{set_digit_value(event->keyval - GDK_0); return true;}
		else if(event->keyval == GDK_KP_Decimal || event->keyval == GDK_period)
			{set_digit_value(10); return true;}
		else if(event->keyval == GDK_Delete || event->keyval == GDK_BackSpace)
			{set_digit_value(-1); return true;}
	}
	switch (event->keyval){
		case GDK_Return:
		case GDK_KP_Enter:
			if(!atom->curs->status) p_edit_digits(true);
			else return_cursor();
			return true;

		case GDK_Escape:
			p_edit_digits(false);
			return true;

		case GDK_Left:
			vect=left;
			if(atom->curs->status)
				p_move_cursor(vect);
			else if(event->state & GDK_CONTROL_MASK)
				u_shift_puzzle(vect);
			else if(event->state & GDK_SHIFT_MASK){
				if(0 > m_save_puzzle(true))
				break;
				m_demo_set_next(-1);
			}
			return true;

		case GDK_Right:
			vect=right;
			if(atom->curs->status)
				p_move_cursor(vect);
			else if(event->state & GDK_CONTROL_MASK)
				u_shift_puzzle(vect);
			else if(event->state & GDK_SHIFT_MASK){
				if(0 > m_save_puzzle(true))
				break;
				m_demo_set_next(1);
			}
			return true;

		case GDK_Up:
			vect=up;
			if(atom->curs->status)
				p_move_cursor(vect);
			else if(event->state & GDK_CONTROL_MASK)
				u_shift_puzzle(vect);
			return true;

		case GDK_Down:
			vect=down;
			if(atom->curs->status)
				p_move_cursor(vect);
			else if(event->state & GDK_CONTROL_MASK)
				u_shift_puzzle(vect);
			return true;

		default:
#ifdef DEBUG
			if (event->state & GDK_SHIFT_MASK){
				printf("key pressed: %s%d\n", "shift + ", event->keyval);
			}
			else if (event->state & GDK_CONTROL_MASK){
				printf("Code key pressed: %s%d\n", "ctrl + ", event->keyval);
			}
			else{
				fprintf(stderr, "Code key pressed: %d\n", event->keyval);
			}
#endif
		break;
	}
	return false;
}

void change_tool_style(int btn){
	static char count = 1;
	if(btn == 1) {count ++; if(count > 2) count = 0;}
	else if(btn == 3) { if(count == 0) count = 3; count --;}
	if (!count)
		gtk_toolbar_set_style(GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH);
	else if (count == 1)
		gtk_toolbar_set_style(GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);
	else if (count == 2)
		gtk_toolbar_set_style(GTK_TOOLBAR (toolbar), GTK_TOOLBAR_TEXT);
}

gboolean button_release (GtkWidget*a, GdkEventButton *event, gpointer data){
	p_botton_release(event);
	return false;
}

gboolean button_press (GtkWidget* widget, GdkEventButton *event, gpointer data){
	int indx;
	if(atom){
		atom->x_cell = (event->x - atom->x_begin) / cell_space;
		atom->y_cell = (event->y - atom->y_begin) / cell_space;
		// проверка на вхождение в оцифровку слева
		if(	atom->x_cell <	atom->left_dig_size &&
			atom->y_cell >=	atom->top_dig_size &&
			atom->y_cell <	atom->y_puzzle_size){
			indx = (atom->y_cell - atom->top_dig_size) * atom->left_dig_size +
					(atom->left_dig_size - atom->x_cell - 1);
			p_digits_button_event(&atom->left, indx,	event);
			return TRUE;
		}
		// проверка на вхождение в оцифровку сверху
		if(	atom->x_cell >=	atom->left_dig_size &&
			atom->x_cell <	atom->x_puzzle_size &&
			//atom->y_cell >=	0 &&
			atom->y_cell <	atom->top_dig_size){
			indx = (atom->x_cell - atom->left_dig_size) *
					atom->top_dig_size + atom->top_dig_size - atom->y_cell - 1;
			p_digits_button_event(&atom->top, indx,	event);
			return TRUE;
		}
		// завершаю редактирование оцифровки при любом щелчке вне её
		if(atom->curs->status){
			if(event->button == 1)// && event->type==GDK_2BUTTON_PRESS)
				p_edit_digits(false);
		}
		// проверка на вхождение в матрицу
		if(	atom->x_cell >=	atom->left_dig_size &&
			atom->x_cell <	atom->x_puzzle_size &&
			atom->y_cell >=	atom->top_dig_size&&
			atom->y_cell <	atom->y_puzzle_size){
			indx = atom->x_cell - atom->left_dig_size +
				(atom->y_cell - atom->top_dig_size) * atom->x_size;
			p_matrix_button_event(indx, event);
			return TRUE;
		}
		// проверка на вхождение в миниатюру
		if(	atom->x_cell <	atom->left_dig_size &&
			atom->y_cell <	atom->top_dig_size){	// меняю стиль тулбара
			change_tool_style(event->button);
			return TRUE;
		}
	}
	return FALSE;
}

void enable_menu_item(int *ptr, int cnt, gboolean state){
	int i;
	GtkWidget* wig;
	for(i=0; i<cnt; i++){
		wig = menu_item[ptr[i]];
		if(ptr[i] < IDM_TOTAL)
			gtk_widget_set_sensitive(wig, state);
	}
}

void enable_toolbar_item(int *ptr, int cnt, gboolean state){
	for(int i=0; i<cnt; i++)
		if(!tool_bar_lock)	toolbar_save_state[i] = state;
		else	gtk_widget_set_sensitive(tool_item[ptr[i]], state);
}

void enable_toolbar_animate(bool state){		// блокировка тулбара пока идет анимация false - сохранить\отключить
	if(tool_bar_lock == state) return;			// триггер блокировки повторений
	tool_bar_lock = state;
	uint i;
	for(i=0; i<IDT_TOTAL; i++) if(i != IDT_SOLVE-IDT_BEGIN){
		if(!state){							// сохраняю состояние тулбара
				toolbar_save_state[i] = gtk_widget_is_sensitive(tool_item[i]);
				gtk_widget_set_sensitive(tool_item[i], false);
		}
		else	gtk_widget_set_sensitive(tool_item[i], toolbar_save_state[i]);
	}
	GtkWidget* wig;
//	GtkItemFactory *item_factory = gtk_item_factory_from_widget(menubar);
	for(i=IDM_BEGIN; i<=IDM_ABOUT; i++){
		wig = menu_item[i];
		if(!state){
				menu_save_state[i-IDM_BEGIN] = gtk_widget_is_sensitive(wig);
				gtk_widget_set_sensitive(wig, false);
		}
		else 	gtk_widget_set_sensitive(wig, menu_save_state[i-IDM_BEGIN]);
	}
}
					// state		// why: digits, matrix, full
void toggle_changes(bool state, pr why){		// маркировка изменения в файле
	if(why == matrix && state == change_matrix) return;
	else if(why == digits && state == change_digits) return;
	if(why == matrix)		change_matrix = state;
	else if(why == digits)	change_digits = state;
	else					change_matrix = change_digits = state;

	state = change_matrix | change_digits;

	m_set_title();

	gtk_widget_set_sensitive(menu_item[IDM_SAVE], state);
//	gtk_widget_set_sensitive(tool_item[IDT_SAVE-IDT_BEGIN], state);
}

const char *pname[]={"toolbar","statusbar","palette","grid","grid5",
							"animate","lesson","viewname","help_level","last_file"};
char *config_buf;
unsigned long pbuflen;

void save_parameter(){
	char *ptr = config_buf = MALLOC_BYTE(MAX_PATH * 2 + 10);
	for(param i=ptoolbar; i<=viewname; i++){
		sprintf(ptr, "%s = %s\n", pname[i], parameter[i] ? "true" : "false");
		ptr = config_buf + strlen(config_buf);
	}
	sprintf(ptr, "%s = %d\n", pname[help_lev], help_level);
	ptr = config_buf + strlen(config_buf);
	ftype = m_get_file_type(last_opened_file);
	if(ftype == mem || g_file_test(last_opened_file, G_FILE_TEST_EXISTS))
		sprintf(ptr, "%s = %s\n", pname[pfile], last_opened_file);
	ptr = config_buf + strlen(config_buf);
	FILE *ff = fopen(config_file, "w");
	if(ff != NULL){
		fwrite(config_buf, 1, strlen(config_buf), ff);
		fclose(ff);
	}
	else fprintf(stderr,_("Config file: %s opening error:(\n"), config_file);
}

char *get_parameter(param x){
	char *ptr = strstr(config_buf, pname[x]);
	if(!ptr) return NULL;
	ptr = strstr(ptr, "=");
	if(!ptr) return NULL;
	while(*ptr == '=' || *ptr == ' ') ptr++;
	int count=0;
	while(ptr[count] != '\n' && ptr[count] && count < (int)pbuflen)  count++;
	if(!count) return NULL;
	char *ret = malloc(count+1);
	for(int i=0; i<count; i++) ret[i]=ptr[i];
	ret[count]=0;
	return ret;
}

bool get_bool_parameter(param x){
	char *ptr = get_parameter(x);
	if(!ptr) return true;
	bool ret = (bool)strcmp(ptr, "false");
	m_free(ptr);
	return ret;
}

byte get_byte_parameter(param x){
	char *ptr = get_parameter(x);
	if(!ptr) return 0;
	return (byte)l_atoi(ptr);
}

void corrected_last_error(){
	// создаю файл-семафор нормального завершения работы программы
	char *buf = malloc(MAX_PATH);
	char mes[] = "1 fail! =(";
	sprintf(buf, "%s%s", config_path, LOCK_FILE);
	FILE *ff;
	if(g_file_test(buf, G_FILE_TEST_EXISTS)){
		ff = fopen(buf, "r");
		if(ff) fread(mes, 10, 1, ff);
		fclose(ff);
		byte a = mes[0] - '0';
		if(a > 3) *last_opened_file = '\0';		// предупреждаю открытие файла если была ошибка
		if(a > 5)
			if(xz(false)) puts("Decompressing done");
		mes[0]++;
		ff = fopen(buf, "w");
		if(ff) fwrite(mes, 1, 10, ff);
		fclose(ff);
	}
	else {
		ff = fopen(buf, "w");
		if(ff) fwrite(mes, 1, 10, ff);
		fclose(ff);
	}
	free(buf);
}

void m_load_parameter(){	// Установка сохранённых параметров
	FILE *ff = fopen(config_file, "r");
	parameter = malloc(10 * sizeof(gboolean));
	int i;
	cell_space = DEF_CELL_SPACE;				// размер ячейки
	demo_flag = false;	//tool  stat  palet grid  grid5 anima  lesson viewname
	gboolean table[10] = {true, true, true, true, true, true,  false, false, false, false};
	for(i=0; i<10; i++)	parameter[i] = table[i];

	if(ff != NULL){
		fseek(ff, 0, SEEK_END);	// читаю параметры в буфер
		pbuflen = ftell(ff);
		fseek(ff,0,SEEK_SET);
		config_buf = malloc(pbuflen+1);
		fread(config_buf, 1, pbuflen, ff);
		config_buf[pbuflen]=0;
		for(param i=ptoolbar; i<=viewname; i++) parameter[i] = get_bool_parameter(i);
		help_level = get_byte_parameter(help_lev);
		char *buf;
		if(!strlen(last_opened_file) || !g_file_test(last_opened_file, G_FILE_TEST_EXISTS)){
			buf = get_parameter(pfile);
			if(buf){
				strcpy(last_opened_file, buf);
				if(strstr(last_opened_file, "db/") == last_opened_file)
					last_opened_number = (int)m_get_base_number_file(last_opened_file);
				m_free(buf);
			}
			else strcpy(last_opened_file, home_dir);
		}
		m_free(config_buf);
		fclose(ff);
		}
	else {
		fprintf(stderr, _("Can't opening config file: %s\n"), config_file);
		system("xdg-mime default matrix.desktop application/matrix");	// привязываюсь к MIME
		strcpy(last_opened_file, home_dir);
	}
}

bool m_test_lib(){
	uint i;
	if((!m_demo_get_list_files()) || demo_count == 0){
		sprintf(last_error, _("No puzzle files found in folder: %s\n%s"), puzzle_path, _("Please update library!"));
		for(i=IDM_LOAD_RANDOM; i<=IDM_HTTP_OPEN; i++)
			gtk_widget_set_sensitive(menu_item[i], false);
		return false; //m_lib_error();
	}
	for(i=IDM_LOAD_RANDOM; i<=IDM_HTTP_OPEN; i++)
		gtk_widget_set_sensitive(menu_item[i], true);
	return true;
}

void m_init_menu_and_button(){
	GtkWidget *wig = menu_item[IDM_TOOLBAR];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[ptoolbar]);

	wig = menu_item[IDM_STATUSBAR];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[pstatusbar]);

	wig = menu_item[IDM_GRID];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[pgrid]);
	gtk_toggle_tool_button_set_active(
				(GtkToggleToolButton*)tool_item[IDT_GRID], parameter[pgrid]);

	wig = menu_item[IDM_GRID5];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig), parameter[pgrid5]);
	gtk_toggle_tool_button_set_active(
				(GtkToggleToolButton*)tool_item[IDT_GRID5], parameter[pgrid5]);

	wig = menu_item[IDM_ANIMATE];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[panimate]);

	wig = menu_item[IDM_LESSON];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[plesson]);

	wig = menu_item[IDM_VNAME];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig),parameter[viewname]);

	wig = menu_item[IDM_HELP_0 + help_level];
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wig), true);

	int tools[] = {IDT_UNDO, IDT_REDO};
	enable_toolbar_item(tools, 2, false);
	m_test_lib();
}

void set_window_size(bool maximaze){
//	if(surface){		// удаляю поверхность рисования миниатюры
//		cairo_surface_destroy (surface);
//		surface = NULL;
//	}
//	if(cr) {cairo_destroy(cr); cr = NULL;}
// подгоняю размер окна при большом размере матрицы
	GdkRectangle workarea = {0};
	gdk_monitor_get_workarea(
		gdk_display_get_primary_monitor(gdk_display_get_default()), &workarea);
	gint width_screen = workarea.width;
	gint height_screen = workarea.height;
	gint x_window_size	= cell_space * atom->x_puzzle_size - 6;
	gint y_window_size	= cell_space * atom->y_puzzle_size + non_area_y_size - 6;
	int x_space = cell_space, y_space = cell_space;

	if(maximaze || x_window_size > width_screen)
		x_space = width_screen / atom->x_puzzle_size;
	if(maximaze || y_window_size > height_screen)
		y_space = (height_screen - non_area_y_size) / atom->y_puzzle_size;
	cell_space = MIN(x_space, y_space);
	cell_space = MIN(cell_space, DEF_CELL_SPACE);
	x_window_size	= cell_space * atom->x_puzzle_size - 6;
	y_window_size	= cell_space * atom->y_puzzle_size + non_area_y_size - 6;
	gtk_window_resize((GtkWindow *)window, x_window_size, y_window_size);
	gtk_widget_queue_draw(draw_area);// обновляю draw_area
}

//**********************************************************************
//**********************************************************************

void on_window_destroy (GObject *object, gpointer user_data){
	quit_program(false);
}

void m_new_file (GObject *object, gpointer user_data){
	if(m_save_puzzle(true) >= 0)
		m_new_puzzle();
}

void m_open_file (GObject *object, gpointer user_data){
	char *buf = malloc(256);
	while(!m_open_puzzle(true, false, NULL)){
		sprintf(buf, _("Error opening file: %s\nFile format not suitable!"), last_opened_file);
		d_message(_("Puzzle error!"), buf, 0);
	}
	m_free(buf);
}

void m_save_file (GObject *object, gpointer user_data){
	m_save_puzzle(false);
}

void m_save_as_file (GObject *object, gpointer user_data){
	m_save_as();
}

void m_print_file (GObject *object, gpointer user_data){
	print_page();
}

void m_features_file (GObject *object, gpointer user_data){
	c_view_status();
}

void m_undo_edit(){
	u_undo();
}

void m_redo_edit(){
	u_redo();
}

void m_invert_edit(){
	m_invert();
}

void m_rotate_left(){
	u_shift_puzzle(left);
}
void m_rotate_right(){
	u_shift_puzzle(right);
}
void m_rotate_up(){
	u_shift_puzzle(up);
}
void m_rotate_down(){
	u_shift_puzzle(down);
}

void m_property_edit(){
	u_change_properties();
}

void m_more_view(){
	m_update_view(1);
}
void m_less_view(){
	m_update_view(-1);
}
void m_norm_view(){
	m_normal();
}

void m_toolbar(GtkCheckMenuItem *chb, gpointer user_data){
	if (gtk_check_menu_item_get_active (chb)){
		parameter[ptoolbar] = true;
		gtk_widget_show(toolbar);
	}
	else {
		gtk_widget_hide(toolbar);
		parameter[ptoolbar] = false;
	}
}

void m_statusbar(GtkCheckMenuItem *chb, gpointer user_data){
	if (gtk_check_menu_item_get_active (chb)){
		gtk_widget_show(statusbar);
		parameter[pstatusbar] = true;
	}
	else {
		gtk_widget_hide(statusbar);
		parameter[pstatusbar] = false;
	}
}

void m_palette(GtkCheckMenuItem *chb, gpointer user_data){
	if(!tool_item[0]) return;
	bool state = gtk_check_menu_item_get_active (chb);
	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)tool_item[IDT_PALETTE], state);
	d_palette_show(state);
}

void m_grid(GtkCheckMenuItem *chb, gpointer user_data){
	if(!tool_item[0]) return;
	parameter[pgrid] = gtk_check_menu_item_get_active (chb);
	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)tool_item[IDT_GRID], parameter[pgrid]);
	gtk_widget_queue_draw(draw_area);
}

void m_grid_5(GtkCheckMenuItem *chb, gpointer user_data){
	if(!tool_item[0]) return;
	parameter[pgrid5] = gtk_check_menu_item_get_active (chb);
	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)tool_item[IDT_GRID5], parameter[pgrid5]);
	gtk_widget_queue_draw(draw_area);
}

void m_solve(){
	c_solver();
	u_after(false);
	toggle_changes(false, matrix);
}

void m_calc_digit(){
	if(u_digits_updates(true) == true) gtk_widget_queue_draw(draw_area);
}

void m_make_overlap(){
	if(solved) return;
	c_make_overlapping(shadow_map, true);
	u_after(false);
	p_update_matrix();
}

void m_animate(GtkCheckMenuItem *chb, gpointer user_data){
	parameter[panimate] = gtk_check_menu_item_get_active (chb);
}

void m_lesson(GtkCheckMenuItem *chb, gpointer user_data){
	parameter[plesson] = gtk_check_menu_item_get_active (chb);
	if(c_solve_buffers) for(int i=0;i<atom->map_size;i++) shadow_map[i]=EMPTY;
	gtk_widget_queue_draw(draw_area);
}

void m_vname(GtkCheckMenuItem *chb, gpointer user_data){
	parameter[viewname] = gtk_check_menu_item_get_active (chb);
	m_set_title();
}

void m_load_random(){
	if(m_save_puzzle(true) >= 0){
		srand(time(NULL));
		place = memory;
		l_open_random_puzzle(false);
	}
}

void m_load_random_color(){
	if(m_save_puzzle(true) >= 0){
		srand(time(NULL));
		place = memory;
		l_open_random_puzzle(true);
	}
}

void m_search_in_lib(){
	if(m_save_puzzle(true) >= 0) l_find_puzzle();
}

void m_demo(){
	if(m_save_puzzle(true) >= 0) if(!m_demo_start()) d_message(_("Error demo begin"), last_error, 0);
}

void m_library_next(){
	if(m_save_puzzle(true) >= 0) m_demo_set_next(1);
}

void m_library_prev(){
	if(m_save_puzzle(true) >=0)	m_demo_set_next(-1);
}

void m_help(){
	const char *uri = "xdg-open https://ru.wikipedia.org/wiki/Японский_кроссворд";
	system(uri);
}

void m_about(){
	d_about_dialog();
}

void m_help_level_0(){
	g_print("RadioButton0 select\n");
}
void m_help_level_1(){
	g_print("RadioButton1 select\n");
}
void m_help_level_2(){
	g_print("RadioButton2 select\n");
}
void m_help_level_3(){
	g_print("RadioButton3 select\n");
}
void m_help_level_4(){
	g_print("RadioButton4 select\n");
}

void m_paint_pen(){
	ps = pen;
}
void m_paint_rectangle(){
	ps = rectangle;
}
void m_paint_filling(){
	ps = filling;
}

void m_grid_button_press(GtkToggleToolButton *tb, gpointer user_data){
	parameter[pgrid] = gtk_toggle_tool_button_get_active(tb);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item[IDM_GRID]),parameter[pgrid]);
	gtk_widget_queue_draw(draw_area);
}

void m_grid5_button_press(GtkToggleToolButton *tb, gpointer user_data){
	parameter[pgrid5] = gtk_toggle_tool_button_get_active(tb);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item[IDM_GRID5]),parameter[pgrid5]);
	gtk_widget_queue_draw(draw_area);
}

void m_palette_button(GtkToggleToolButton *tb, gpointer user_data){
	palette_show = gtk_toggle_tool_button_get_active(tb);
	gtk_check_menu_item_set_active((GtkCheckMenuItem*)menu_item[IDM_PALETTE], palette_show);
	d_palette_show(palette_show);
}

void m_mouse_motion(GtkWidget *widget, GdkEventMotion *event){
	p_mouse_motion(widget, event);
}

void m_get_menu_item(GtkBuilder* builder){
	uint i;
	gchar *menu_item_name[] = {"IDM_NEW","IDM_OPEN","IDM_SAVE","IDM_SAVE_AS","IDM_EXPORT","IDM_PRINT",
	"IDM_FEATURES","IDM_PROPERTIS","IDM_QUIT","IDM_UNDO","IDM_REDO","IDM_INVERT","IDM_PASTE","IDM_ROTATE_LEFT",
	"IDM_ROTATE_RIGHT","IDM_ROTATE_UP","IDM_ROTATE_DOWN","IDM_CLEAR_DIGITS","IDM_CLEAR_PUZ_MARK","IDM_CLEAR_DIG_MARK","IDM_CLEAR_PUZZLE","IDM_MORE","IDM_LESS","IDM_NORMAL","IDM_TOOLBAR","IDM_STATUSBAR","IDM_PALETTE","IDM_GRID","IDM_GRID5","IDM_SOLVE","IDM_CALC_DIG","IDM_MAKE_OVERLAPPED","IDM_ANIMATE","IDM_LESSON","IDM_VNAME","IDM_HELP_0","IDM_HELP_1","IDM_HELP_2","IDM_HELP_3","IDM_HELP_4","IDM_LOAD_RANDOM","IDM_LOAD_RANDOM_C","IDM_SEARCH_IN_LIBRARY","IDM_DEMO_START","IDM_LIBRARY_NEXT","IDM_LIBRARY_PREV","IDM_HTTP_OPEN","IDM_HELP","IDM_ABOUT"};
	for(i=0; i<IDM_TOTAL; i++)
		menu_item[i] = GTK_WIDGET (gtk_builder_get_object (builder, menu_item_name[i]));

	gchar *tool_item_name[] = {"IDT_UNDO","IDT_REDO","IDT_SOLVE","IDT_CALC_DIG","IDT_CLEAR_PUZZLE","IDT_PEN_PAINT",
	"IDT_RECT_PAINT","IDT_FILL_PAINT","IDT_PALETTE","IDT_GRID","IDT_GRID5"};
	for(i=0; i<IDT_TOTAL; i++)
		tool_item[i] = GTK_WIDGET (gtk_builder_get_object (builder, tool_item_name[i]));
}

//**********************************************************************
//**********************************************************************

int main( int argc, char *argv[] ){
	// инициал. глобальных переменных
	atom = NULL;
	layout = NULL;
	surface = NULL;					// контекст рисования миниатюры
	palette = NULL;
	cr = NULL;
	ps = pen;
	thread_get_library = 0;
	last_opened_number = l_base_stack_top = 0;
	p_block_view_port_x = -1;
	c_solve_buffers = false;
	l_jch_container = false;
	tool_item[0] = NULL;

	char *lock_db = malloc(MAX_PATH);// удаляю возможно оставщийся файл блокировки библиотеки
	sprintf(lock_db, "%s/%s", config_path, "lock_db");
//	if(g_file_test(lock_db, G_FILE_TEST_EXISTS))
	remove(lock_db);
	free(lock_db);

	// инициал. генератора сл.чисел
	srand (time(NULL));
	// подключаю локализацию
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	my_threads_init();
	my_threads_enter();
	gtk_init (&argc, &argv);
	gdk_init (&argc, &argv);
	// инициал. libnotify
	notify_init(CLASS_NAME);

	// инициализация буферов путей и имён
	last_error = MALLOC_BYTE(2048);
	last_opened_file = MALLOC_BYTE(MAX_PATH);
	puzzle_path= MALLOC_BYTE(MAX_PATH);
	db_path = MALLOC_BYTE(MAX_PATH);
	puzzle_name = MALLOC_BYTE(MAX_PATH);
	home_dir = g_get_home_dir();
	config_path = malloc(strlen(home_dir)+sizeof(CONFIG_PATH)+1);
	config_file = malloc(strlen(home_dir)+sizeof(CONFIG_PATH)+sizeof(CONFIG_NAME)+1);
	sprintf(config_path, "%s%s", home_dir, CONFIG_PATH);
	sprintf(config_file, "%s%s%s", home_dir, CONFIG_PATH, CONFIG_NAME);
	sprintf(puzzle_path, "%s/%s", home_dir, WORK_NAME);
	sprintf(db_path, "%s%s/%s", home_dir, CONFIG_PATH, "db");
	if(!g_file_test(config_path, G_FILE_TEST_IS_DIR)) mkdir(config_path, 0766);
	if(!g_file_test(puzzle_path, G_FILE_TEST_IS_DIR)) mkdir(puzzle_path, 0766);

	GError	 *gerror = NULL;
	GdkPixbuf  *pixbuf = gdk_pixbuf_new_from_file (ICON_PATH, &gerror);
	sys_icon = gdk_pixbuf_scale_simple (pixbuf, ICON_SIZE, ICON_SIZE, GDK_INTERP_BILINEAR);

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, "main.glade", NULL);

    window		= GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
    statusbar	= GTK_WIDGET (gtk_builder_get_object (builder, "ID_STATUSBAR"));
	draw_area	= GTK_WIDGET (gtk_builder_get_object (builder, "ID_DRAW_AREA"));
	scroll_win	= GTK_WIDGET (gtk_builder_get_object (builder, "ID_SCROLLED"));
	menubar		= GTK_WIDGET (gtk_builder_get_object (builder, "ID_MENUBAR"));
	toolbar		= GTK_WIDGET (gtk_builder_get_object (builder, "IDT_TOOLBAR"));
	m_get_menu_item(builder);
	m_load_parameter();			// создаю структуру пазла и читаю параметры

    gtk_builder_connect_signals (builder, NULL);

	g_object_unref (G_OBJECT (builder));

//	gtk_window_set_title (GTK_WINDOW(window), CAPTION_TEXT);
	gtk_window_set_default_icon (sys_icon);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_scrolled_window_set_policy((GtkScrolledWindow*)scroll_win,
							GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_set_border_width (GTK_CONTAINER (window), BORDER_WIDTH);

//    gtk_widget_realize(window);

	set_status_bar("Program starting");
	l_list_mfree(true);			// инициализирую переменные библиотеки

	l_lib_lock(true);
	if(!l_base_load_from_file(true))// загружаю библиотеку
		l_get_library(false);		// и подгружаю обновления с сайта
	l_lib_lock(false);

	u_init();					// инициализирую undo-redo
	c_reset_puzzle_pointers();	// инициализация решалки

	GtkAllocation allocation;
	gtk_widget_get_allocation (menubar, &allocation);
	non_area_y_size = allocation.height;
	gtk_widget_get_allocation (toolbar, &allocation);
	non_area_y_size += allocation.height;
	gtk_widget_get_allocation (statusbar, &allocation);
	non_area_y_size += allocation.height;
	corrected_last_error();		// проверка нормального завершения предыдущего сеанса
	if(argc > 1 && g_file_test(argv[1], G_FILE_TEST_EXISTS)){
		strcpy(last_opened_file, argv[1]);
		place = exfile;
		m_open_puzzle(false, false, NULL);
	}
	else if(!*last_opened_file){// открываю файл пазла
		place = memory;
		m_get_puzzle_default();
	}
	else if(last_opened_number){
		jch_arch_item *o = l_get_item_from_base(last_opened_number);
		place = memory;
		if(o) m_open_puzzle(false, false, o->body);
		else m_get_puzzle_default();
	}
	else {
		place = exfile;
		m_open_puzzle(false, false, NULL);
	}
	m_init_menu_and_button();
	gtk_widget_show_all(window);
	signal(SIGSEGV, posix_death_signal); // обработчик исключений SIGSEGV
//	signal(SIGABRT, posix_abort_signal); // обработчик исключений SIGABRT
	gtk_main();

	my_threads_leave();
	u_state_free();
	notify_uninit();
//	free(tb);
	free(last_error);
	free(last_opened_file);
	free(puzzle_path);
	free(db_path);
	free(puzzle_name);
	free(config_path);
	free(config_file);
	free(parameter);
	return(0);
}
